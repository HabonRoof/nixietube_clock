"""IN-4 nominal reconstruction. Run with FreeCAD's Python runtime."""
import sys, json, math
from pathlib import Path
sys.path.append('/Applications/FreeCAD.app/Contents/Resources/lib')
import FreeCAD as App, Part
V = App.Vector
OUT = Path(__file__).resolve().parent
P = dict(glass_diameter=30.0, glass_height=31.0, pin_circle_diameter=18.0,
         pin_diameter=1.0, pin_length=7.0, exhaust_length=10.0,
         digit_height=17.0, wall=0.8)
doc=App.newDocument('IN4_Nominal')
parts=[]
def add(name, shape, material, group):
    assert not shape.isNull() and shape.isValid(), name
    o=doc.addObject('PartDesign::Feature',name); o.Label=name; o.Shape=shape
    o.addProperty('App::PropertyString','MaterialNote'); o.MaterialNote=material
    o.addProperty('App::PropertyString','AccuracyNote'); o.AccuracyNote='Nominal reconstruction; internal geometry estimated from photographs'
    parts.append((o,material,group)); return shape
def cyl(r,h,x=0,y=0,z=0): return Part.makeCylinder(r,h,V(x,y,z))
def revolved(points):
    edges=[]; last=V(*points[0])
    for p in points[1:]:
        if len(p)==2:
            mid,end=p; edge=Part.Arc(last,V(*mid),V(*end)).toShape(); last=V(*end)
        else:
            end=V(*p); edge=Part.makeLine(last,end); last=end
        edges.append(edge)
    return Part.Face(Part.Wire(edges)).revolve(V(0,0,0),V(0,0,1),360)
# Rounded cylindrical envelope, shallow domed viewing end, sealed foot.
outer=revolved([(0,0,0),(13,0,0),((14.414,0,.586),(15,0,2)),(15,0,25),
                 ((13.243,0,29.243),(9,0,31)),(0,0,31),(0,0,0)])
inner=revolved([(0,0,1.8),(12.7,0,1.8),((13.761,0,2.239),(14.2,0,3.3)),(14.2,0,25),
                 ((12.677,0,28.677),(9,0,30.2)),(0,0,30.2),(0,0,1.8)])
add('Glass_envelope',outer.cut(inner),'glass','Envelope')
tip=revolved([(0,0,-10),((.565685,0,-9.765685),(.8,0,-9.2)),(1.8,0,-7.8),(2,0,-2),
              ((2.3,0,-.6),(3,0,0)),(0,0,0),(0,0,-10)])
add('Exhaust_tip_estimated',tip,'glass','Envelope')
pin_positions=[]
for i in range(14):
    a=math.radians(33+24.5*i); x=9*math.sin(a); y=9*math.cos(a)
    pin_positions.append(dict(pin=i+1,x=x,y=y))
    s=cyl(.5,6.5,x,y,-6.5).fuse(Part.makeSphere(.5,V(x,y,-6.5)))
    add('Pin_%02d'%(i+1),s,'pin','Pins')
    add('Feedthrough_%02d'%(i+1),cyl(.9,.5,x,y,1.8),'seal','Internals')
add('Mica_base_estimated',cyl(12.6,.35,z=2.5),'mica','Internals')
for y in (-11.5,11.5):
    add('Ceramic_support_'+str(y).replace('-','minus'),cyl(.85,23.5,0,y,3),'ceramic','Internals')
    add('Ceramic_cap_'+str(y).replace('-','minus'),cyl(1.55,1.2,0,y,26.6).fuse(Part.makeSphere(1.55,V(0,y,27.8)).common(cyl(2,1.55,0,y,27.8))),'ceramic','Internals')
# Two split cylindrical anode bands visible in the user's side views.
for k,z in enumerate((7,18)):
    ring=cyl(12.5,6,z=z).cut(cyl(12.27,6,z=z))
    ring=ring.cut(Part.makeBox(3.2,30,8,V(-1.6,-15,z-1)))
    add('Anode_band_%d'%k,ring,'darkmetal','Internals')
ring=cyl(13.1,.23,z=26.5).cut(cyl(12.2,.23,z=26.5))
add('Front_grid_rim',ring,'metal','Grid')
grid=[]
for i in range(-8,9):
    t=i*1.45; reach=math.sqrt(12.7**2-t*t)
    grid.append(Part.makeCylinder(.065,2*reach,V(-reach,t,26.8),V(1,0,0)))
    grid.append(Part.makeCylinder(.065,2*reach,V(t,-reach,26.94),V(0,1,0)))
add('Anode_square_mesh',Part.makeCompound(grid),'metal','Grid')
# Independent explicit Bezier strokes, fused into one solid per cathode.
from digit_profiles import ORDER, PROFILES
order=ORDER
layer_report=[]
def rod(a,b,r=.11):
    a,b=V(*a),V(*b); d=b-a; return Part.makeCylinder(r,d.Length,a,d)
def make_digit(digit,z):
    if digit=='8':
        loops=[]
        for stroke in PROFILES[digit]:
            start=V(stroke[0][1],stroke[0][2],z); origin=start; edges=[]
            for command in stroke[1:]:
                end=V(command[-2],command[-1],z)
                curve=Part.BezierCurve()
                curve.setPoles([start,V(command[1],command[2],z),V(command[3],command[4],z),end])
                edges.append(curve.toShape()); start=end
            wire=Part.Wire(edges)
            profile=Part.Wire(Part.makeCircle(.13,origin,V(-1,0,0)))
            loops.append(wire.makePipeShell([profile],True,False))
        return loops[0].fuse(loops[1],.00001).removeSplitter()
    pieces=[]
    for stroke in PROFILES[digit]:
        start=V(stroke[0][1],stroke[0][2],z)
        pieces.append(Part.makeSphere(.13,start))
        for command in stroke[1:]:
            end=V(command[-2],command[-1],z)
            if command[0]=='L':
                pieces.append(rod(tuple(start),tuple(end),.13))
            else:
                poles=[start,V(command[1],command[2],z),V(command[3],command[4],z),end]
                curve=Part.BezierCurve(); curve.setPoles(poles)
                path=Part.Wire(curve.toShape())
                profile=Part.Wire(Part.makeCircle(.13,start,curve.tangent(0)[0]))
                pieces.append(path.makePipeShell([profile],True,False))
            pieces.append(Part.makeSphere(.13,end)); start=end
    result=pieces[0]
    for n,piece in enumerate(pieces[1:]):
        result=result.fuse(piece,0.00001)
        if not result.isValid(): print("Join failure",digit,n,"piece valid",piece.isValid(),flush=True)
    return result.removeSplitter()
for layer,digit in enumerate(order):
    z=10+layer*1.65
    shape=make_digit(digit,z)
    print('Digit',digit,'valid',shape.isValid(),'solids',len(shape.Solids),flush=True)
    assert shape.isValid() and len(shape.Solids)==1, ('digit',digit)
    bounds=shape.optimalBoundingBox()
    assert 16 < bounds.YLength < 17.6, ('incomplete numeral',digit,bounds)
    assert bounds.ZLength < .261, ('nonplanar digit',digit,bounds)
    add('Cathode_digit_'+digit,shape,'metal','Digits')
    obj=parts[-1][0]
    obj.addProperty('App::PropertyInteger','LayerFromBottom'); obj.LayerFromBottom=layer+1
    obj.addProperty('App::PropertyLength','CenterHeight'); obj.CenterHeight=z
    layer_report.append(dict(digit=digit,layer_from_bottom=layer+1,z_mm=z,valid=True,solids=1,bounds_mm=[bounds.XLength,bounds.YLength,bounds.ZLength]))
    # Keep illustrative lead wires outside the numeral's reading area.
    pinmap={'4':1,'6':2,'8':3,'9':5,'7':6,'0':8,'2':9,'3':11,'5':12,'1':14}
    pin=pin_positions[pinmap[digit]-1]
    anchor=PROFILES[digit][0][0]; x,y=anchor[1:]
    side_y=10 if y>=0 else -10
    route=[(pin['x'],pin['y'],2.8),(pin['x'],pin['y'],5),
           (pin['x'],side_y,5),(x,side_y,5),(x,side_y,z),(x,y,z)]
    add('Lead_digit_'+digit,Part.makeCompound([rod(a,b,.1) for a,b in zip(route,route[1:]) if a!=b]),'metal','Leads')
(OUT/'digit_layers.json').write_text(json.dumps(layer_report,indent=2))
doc.recompute()
doc.saveAs(str(OUT/'IN4_nominal.FCStd'))
Part.export([o for o,_,_ in parts],str(OUT/'IN4_detailed.step'))
Part.export([o for o,_,g in parts if g in ('Envelope','Pins')],str(OUT/'IN4_mechanical.step'))
meshes=[]
for o,m,g in parts:
    verts,faces=o.Shape.tessellate(.035)
    meshes.append(dict(name=o.Label,material=m,group=g,vertices=[list(v) for v in verts],faces=faces))
(OUT/'geometry.json').write_text(json.dumps(meshes))
(OUT/'parameters.json').write_text(json.dumps(dict(parameters_mm=P,pins=pin_positions,digit_order_bottom_to_top=order,notes='Nominal coarse-grid variant; photo-derived internals and glass curvature. Pin numbers viewed from below must be checked before PCB use.'),indent=2))
# Reimport STEP and check CAD validity, dimensions and geometry preservation.
loaded=Part.Shape(); loaded.read(str(OUT/'IN4_detailed.step'))
assert loaded.isValid()
bb=loaded.optimalBoundingBox()
assert abs(bb.XLength-30)<.01 and abs(bb.YLength-30)<.01 and abs(bb.ZLength-41)<.01
source=Part.makeCompound([o.Shape for o,_,_ in parts])
print('Volume roundtrip',source.Volume,loaded.Volume,flush=True)
# Curved-face integration differs slightly after STEP surface conversion.
assert abs(source.Volume-loaded.Volume)/source.Volume < 1e-5
report=dict(valid=True,units='mm',bounding_box_mm=[bb.XLength,bb.YLength,bb.ZLength],objects=len(parts),solids=len(loaded.Solids),source_volume_mm3=source.Volume,reimport_volume_mm3=loaded.Volume,relative_volume_tolerance=1e-5,digit_order_bottom_to_top=order)
(OUT/'validation.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report))
