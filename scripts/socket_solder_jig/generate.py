"""Read current PCB, generate a parametric machining concept (not a released drawing)."""
import sys,json,math
from pathlib import Path
P=Path(__file__).resolve().parent
sys.path.insert(0,str(P.parent/'assembly_validation'))
from inspect_boards import inspect
b=inspect('display_board_in4')
fps=b['footprints']; pads=[]
for f in sorted(fps,key=lambda f:f['ref']):
 if f['ref'].startswith('N'):
  for p in f['pads']:
   if p['n']: pads.append({'ref':f['ref'],'pin':p['n'],'x':p['xy'][0]-60,'y':180-p['xy'][1]})
mounts=[[f['at'][0]-60,180-f['at'][1]] for f in fps if f['ref'] in ['H1','H2','H3','H4']]
centers=[[f['at'][0]-60,180-f['at'][1]] for f in sorted(fps,key=lambda f:f['ref']) if f['ref'].startswith('N')]
assert len(pads)==84
assert len(mounts)==4
(P/'coordinates.json').write_text(json.dumps({'status':'CONCEPT: user dimensions recorded; installed height discrepancy and physical fit pending','coordinate_system':'mm; x=KiCadX-60, y=180-KiCadY; viewed from tube side, NOT solder side','pads':pads,'mounts':mounts,'centers':centers},indent=2))
scad='''// MACHINING CONCEPT ONLY. Hot parts: solder-pallet composite, NOT PLA.
// All dimensions mm. Positive Z points from fixture base toward solder side.
$fn=64;
part="assembly"; // assembly, guide, base
index=0; // guide N1..N6 = 0..5
socket_d=1.60; // User measured upper barrel OD; verify batch maximum
clearance=0.04; // diametral; target machined fit, not FDM tolerance
socket_height=6.30; // User measured PCB front to socket mouth
base_t=4;
guide_t=4;
pcb_gap=socket_height-guide_t; // 2.3 measured / 3.0 shoulder-seated; adjustable
guide_d=26;
engagement=guide_t;
assert(guide_t<5); // Do not admit the 1.7 mm shoulder into the 1.64 mm hole
'''
scad+='centers='+json.dumps(centers)+';\nmounts='+json.dumps(mounts)+';\nholes='+json.dumps([[p['x'],p['y']] for p in pads])+';\n'
scad+='''module guide(i){
 difference(){
  cylinder(d=guide_d,h=guide_t);
  for(j=[i*14:i*14+13]) translate([holes[j][0]-centers[i][0],holes[j][1]-centers[i][1],-0.01])
   union(){
    cylinder(d=socket_d+clearance,h=guide_t+0.02);
    translate([0,0,guide_t-0.14]) cylinder(d1=socket_d+clearance,d2=socket_d+clearance+0.30,h=0.15);
   }
  // Two M2 clearance holes; screw guide into tapped base, low heads below top.
  for(x=[-11,11]) translate([x,0,-0.1]) cylinder(d=2.2,h=guide_t+0.2);
 }
}
module base(){difference(){cube([240,60,base_t]);
 for(c=centers) for(x=[-11,11]) translate([c[0]+x,c[1],-0.1]) cylinder(d=1.6,h=base_t+0.2);
 for(m=mounts) translate([m[0],m[1],-0.1]) cylinder(d=2.5,h=base_t+0.2);
}}
if(part=="guide") guide(index);
if(part=="base") base();
if(part=="assembly"){
 color("DimGray") base();
 for(i=[0:5]) translate([centers[i][0],centers[i][1],base_t]) color("SlateGray") guide(i);
 // Reference adjustable insulating standoffs, NOT fixed production dimensions.
 for(m=mounts) translate([m[0],m[1],base_t]) color("Ivory") difference(){
 cylinder(d=6,h=guide_t+pcb_gap); translate([0,0,-.1]) cylinder(d=3.2,h=guide_t+pcb_gap+.2);
 }
}
'''
(P/'jig_concept.scad').write_text(scad)
# 2D machining layouts; see README for thickness and tolerances.
s=['0','SECTION','2','ENTITIES']
for p in pads:s += ['0','CIRCLE','8','SOCKET_GUIDE','10',str(p['x']),'20',str(p['y']),'30','0','40','0.82']
for c in centers:s += ['0','CIRCLE','8','GUIDE_OUTLINE','10',str(c[0]),'20',str(c[1]),'30','0','40','13']
for c in centers:
 for x in [-11,11]:s += ['0','CIRCLE','8','M2_CLEARANCE','10',str(c[0]+x),'20',str(c[1]),'30','0','40','1.1']
s+=['0','ENDSEC','0','EOF'];(P/'guide_layout_CONCEPT.dxf').write_text('\n'.join(s)+'\n')
minspace=min(math.hypot(a['x']-b['x'],a['y']-b['y']) for i,a in enumerate(pads) for b in pads[i+1:])
(P/'validation.json').write_text(json.dumps({'socket_holes':len(pads),'mounts':len(mounts),'minimum_hole_center_spacing_mm':minspace,'diametral_clearance_assumed_mm':.04,'guide_thickness_mm':4.0,'entry_chamfer_axial_mm':0.15,'effective_engagement_mm':3.85,'angular_play_estimate_deg':math.degrees(math.atan(.04/3.85)),'physical_fit_verified':False,'thermal_test_performed':False,'axial_tail_interpretation_confirmed':True,'installed_height_discrepancy_mm':0.7},indent=2))
print('Generated 84 guide holes from current PCB; minimum pitch',minspace)

# Complete base outline + fastener holes. Tapped-hole diameters are pilot sizes.
bd=['0','SECTION','2','ENTITIES']
for a,z in [((0,0),(240,0)),((240,0),(240,60)),((240,60),(0,60)),((0,60),(0,0))]:
 bd += ['0','LINE','8','OUTLINE','10',str(a[0]),'20',str(a[1]),'11',str(z[0]),'21',str(z[1])]
for c in centers:
 for x in [-11,11]:bd += ['0','CIRCLE','8','M2_TAP_PILOT','10',str(c[0]+x),'20',str(c[1]),'40','0.8']
for m in mounts:bd += ['0','CIRCLE','8','M3_TAP_PILOT','10',str(m[0]),'20',str(m[1]),'40','1.25']
bd += ['0','ENDSEC','0','EOF']
(P/'base_CONCEPT.dxf').write_text('\n'.join(bd)+'\n')
