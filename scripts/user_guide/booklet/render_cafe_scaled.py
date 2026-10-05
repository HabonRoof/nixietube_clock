"""Dimensioned cafe scene. Blender coordinate unit = 1 mm (scene scale .001)."""
import bpy,math,random,json
from pathlib import Path
from mathutils import Vector
P=(Path(__file__).resolve().parents[3]/'doc/user_guide/booklet');R=P.parents[2]
bpy.ops.wm.open_mainfile(filepath=str(R/'hardware/case/revision_h/clock_case_revision_h.blend'))
s=bpy.context.scene
# Original clock is preserved at scale 1. Recenter only.
bpy.data.objects['CLOCK_TILT_15deg'].location.x-=125
bpy.data.objects['tilt_base_15deg'].location.x-=125
bpy.data.objects['Studio_tabletop'].hide_render=True
for o in list(s.objects):
 if o.type=='LIGHT':bpy.data.objects.remove(o,do_unlink=True)
def mat(name,col,rough=.5,metal=0):
 m=bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*col,1);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal;return m
wood=bpy.data.materials['Dark walnut solid cradle'].copy();wood.name='Cafe walnut shelf'
cream=mat('Ivory stoneware',(.60,.48,.30),.22);ivory=mat('Cream porcelain',(.72,.68,.53),.18);earth=mat('Warm clay',(.32,.105,.04),.6)
coffee=mat('Coffee',(.022,.009,.004),.22);paper=mat('Book paper',(.65,.55,.37),.85)
colors=[mat('Book ochre',(.29,.15,.035),.9),mat('Book forest',(.035,.075,.038),.9),mat('Book rust',(.23,.065,.027),.9)]
black=mat('Blackened iron',(.035,.027,.018),.26,.8);brass=mat('Patinated brass',(.22,.13,.037),.31,.7)
wall=mat('Warm plaster',(.45,.28,.15),.9)
# Small-scale plaster texture.
p=wall.node_tree.nodes.get('Principled BSDF');nt=wall.node_tree;tex=nt.nodes.new('ShaderNodeTexNoise');tex.inputs['Scale'].default_value=250;bum=nt.nodes.new('ShaderNodeBump');bum.inputs['Strength'].default_value=.18;bum.inputs['Distance'].default_value=.2;nt.links.new(tex.outputs['Fac'],bum.inputs['Height']);nt.links.new(bum.outputs['Normal'],p.inputs['Normal'])
def cube(name,loc,dims,m,bevel=0):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.name=name;o.dimensions=dims;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(m)
 if bevel:mod=o.modifiers.new('Soft edges','BEVEL');mod.width=bevel;mod.segments=3;o.modifiers.new('Weighted normals','WEIGHTED_NORMAL')
 return o
def uv(name,loc,scale,m):
 bpy.ops.mesh.primitive_uv_sphere_add(segments=24,ring_count=12,radius=1,location=loc);o=bpy.context.object;o.name=name;o.scale=scale;o.data.materials.append(m)
 for f in o.data.polygons:f.use_smooth=True
 return o
def lathe(name,x,y,z,profile,m,segments=80):
 verts=[];faces=[]
 for r,h in profile:
  for i in range(segments):a=2*math.pi*i/segments;verts.append((x+r*math.cos(a),y+r*math.sin(a),z+h))
 for j in range(len(profile)-1):
  for i in range(segments):a=j*segments+i;b=j*segments+(i+1)%segments;faces.append((a,b,b+segments,a+segments))
 mesh=bpy.data.meshes.new(name);mesh.from_pydata(verts,[],faces);mesh.materials.append(m);o=bpy.data.objects.new(name,mesh);s.collection.objects.link(o)
 for f in mesh.polygons:f.use_smooth=True
 return o
def curve(name,points,r,m):
 cu=bpy.data.curves.new(name,'CURVE');cu.dimensions='3D';cu.bevel_depth=r;cu.bevel_resolution=3;sp=cu.splines.new('BEZIER');sp.bezier_points.add(len(points)-1)
 for bp,p in zip(sp.bezier_points,points):bp.co=p;bp.handle_left_type='AUTO';bp.handle_right_type='AUTO'
 o=bpy.data.objects.new(name,cu);s.collection.objects.link(o);o.data.materials.append(m);return o
# Furniture: top of main shelf is exactly z=0.
cube('Shelf_1100x320x24_mm',(0,25,-12),(1100,320,24),wood,2)
cube('Plaster wall',(0,213,280),(1600,24,1050),wall,1)
cube('Upper shelf_1100x220x24_mm',(0,101,348),(1100,220,24),wood,2)
# Two solid walnut shelf brackets.
for x in [-390,390]:cube('Shelf support',(x,162,-60),(24,92,100),wood,2)
# Dimensions based on KINTO SCS 250ml specification. Generic geometry, no logo.
def mug(x,y,z,m=ivory):
 lathe('Cup_body_D80_H90_mm',x,y,z,[(0,0),(28,0),(31,4),(34,15),(38,55),(40,85),(40,88),(39,90),(36,90),(35,86),(34,45),(28,8),(0,8)],m)
 curve('Cup_handle_total_width105_mm',[(x+36,y,z+72),(x+53,y,z+74),(x+62,y,z+59),(x+60,y,z+35),(x+48,y,z+25),(x+34,y,z+31)],3,m)
 lathe('Coffee surface',x,y,z,[(0,77),(35.5,77)],coffee)
mug(211,-2,0)
# A5 and B6 books with native paper blocks and cloth covers.
def book(name,x,y,z,w,d,t,m,angle=0):
 root=bpy.data.objects.new(name,None);s.collection.objects.link(root);root.location=(x,y,z);root.rotation_euler.z=angle
 for nm,loc,dim,ma in [('paper',(0,0,t/2),(w-4,d-4,t-3),paper),('bottom',(0,0,.75),(w,d,1.5),m),('top',(0,0,t-.75),(w,d,1.5),m),('spine',(-w/2+1,0,t/2),(2,d,t),m)]:
  o=cube(name+' '+nm,loc,dim,ma,.7);o.parent=root
 return root
book('A5_book_148x210x18_mm',-343,-1,0,148,210,18,colors[0],-.06)
book('B6_book_128x182x22_mm',-337,3,18,128,182,22,colors[1],.08)
# Ceramic pot, soil, stems and curved leaves.
greens=[mat('Plant green '+str(i),c,.42) for i,c in enumerate([(.023,.14,.012),(.045,.22,.025),(.065,.28,.028),(.021,.10,.017)])]
stemmat=mat('Plant stems',(.085,.16,.018),.6);soil=mat('Potting soil',(.026,.012,.005),1)
random.seed(12)
def plant(x,y,z,r=50,h=100):
 lathe('Plant_pot_D100_H100_mm',x,y,z,[(0,0),(r*.65,0),(r*.68,6),(r*.92,h-10),(r,h-7),(r,h),(r*.87,h),(r*.84,h-14),(r*.61,8),(0,8)],earth)
 lathe('Soil',x,y,z,[(0,h-10),(r*.86,h-10)],soil)
 for i in range(13):
  angle=i*2.4;dist=50+random.random()*75;end=Vector((x+math.cos(angle)*dist,y+math.sin(angle)*dist,z+h+60+random.random()*160))
  start=Vector((x,y,z+h-10));middle=(start+end)/2+Vector((0,0,25));curve('Stem',[start,middle,end],1.4,stemmat)
  direction=Vector((math.cos(angle)*.65,math.sin(angle)*.65,-.35)).normalized();side=direction.cross(Vector((0,0,1))).normalized();length=70+random.random()*35;width=27+random.random()*10
  verts=[];faces=[];rows=16
  for j in range(rows+1):
   t=j/rows;mid=end+direction*length*t+Vector((0,0,19*math.sin(t*math.pi)))
   half=width*(math.sin(math.pi*t)**.75)*(1+.04*math.sin(t*30))
   for q in [-1,0,1]:verts.append(tuple(mid+side*q*half+Vector((0,0,-5*abs(q)*math.sin(math.pi*t)))))
  for j in range(rows):
   for k in range(2):a=j*3+k;faces.append((a,a+1,a+4,a+3))
  me=bpy.data.meshes.new('Leaf');me.from_pydata(verts,[],faces);me.materials.append(greens[i%4]);o=bpy.data.objects.new('Leaf',me);s.collection.objects.link(o);mod=o.modifiers.new('Leaf thickness','SOLIDIFY');mod.thickness=.25
  for f in me.polygons:f.use_smooth=True
plant(-190,96,0)
# Small ornamental ceramic bird (height ~45 mm), sits beside the books.
uv('Bird body',(-285,-58,57),(23,15,15),cream);uv('Bird head',(-270,-61,74),(11,10,11),cream)
bpy.ops.mesh.primitive_cone_add(vertices=24,radius1=5,radius2=0,depth=14,location=(-256,-62,74),rotation=(0,math.pi/2,0));bpy.context.object.data.materials.append(brass)
uv('Bird eye',(-268,-70,78),(1.7,1,1.7),black)
# Compact vintage grinder with a dimensioned wooden box, hopper and crank.
cube('Grinder_body_90x80x100_mm',(362,60,50),(90,80,100),wood,4)
cube('Grinder drawer',(362,17,36),(70,5,46),wood,2);uv('Drawer knob',(362,11,36),(6,5,6),black)
lathe('Grinder hopper',362,60,101,[(17,0),(18,18),(37,28),(44,55),(45,60),(40,60),(34,32),(14,20)],black)
curve('Grinder crank',[(362,60,160),(362,60,182),(420,60,182)],3,brass);uv('Grinder handle',(420,60,194),(9,9,16),wood)
# Additional cafe ornaments on upper shelf, sized to real small objects.
for x,m in [(-350,cream),(-222,ivory)]:mug(x,103,360,m)
for x,m in [(198,colors[0]),(255,colors[1]),(318,colors[2])]:
 lathe('Tea tin_D50_H95_mm',x,125,360,[(0,0),(25,0),(25,90),(26,90),(26,95),(0,95)],m)
# Small wooden house, 50 x 38 x 70 mm.
cube('Wood house',(90,110,383),(50,38,46),wood,1)
verts=[(65,91,406),(115,91,406),(90,91,430),(65,129,406),(115,129,406),(90,129,430)];faces=[(0,1,2),(3,5,4),(0,3,4,1),(0,2,5,3),(1,4,5,2)];me=bpy.data.meshes.new('House roof');me.from_pydata(verts,[],faces);me.materials.append(wood);o=bpy.data.objects.new('House roof',me);s.collection.objects.link(o)
cube('House door',(90,89,377),(9,2,25),black,.3)
# Shoji-like glowing window at left; visible timber lattice and warm translucent panes.
win=mat('Warm window paper',(.68,.48,.25),.9);p=win.node_tree.nodes.get('Principled BSDF');p.inputs['Emission Color'].default_value=(1,.66,.28,1);p.inputs['Emission Strength'].default_value=.35
cube('Window paper',(-530,194,280),(300,3,720),win)
for x in [-680,-605,-530,-455,-380]:cube('Window vertical timber',(x,190,280),(9,8,720),wood,1)
for z in [-80,40,160,280,400,520,640]:cube('Window horizontal timber',(-530,189,z),(300,10,8),wood,1)
# Warm light; long focal length avoids exaggerated foreground scale.
def light(name,loc,target,power,color,size):
 d=bpy.data.lights.new(name,'AREA');d.energy=power;d.color=color;d.shape='DISK';d.size=size;o=bpy.data.objects.new(name,d);s.collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
light('Warm afternoon window',(-390,-270,450),(0,40,20),2600000,(1,.73,.42),300)
light('Soft amber fill',(350,-180,350),(0,0,45),900000,(1,.55,.25),250)
light('Soft front bounce',(0,-600,190),(0,0,60),650000,(1,.84,.61),450)
s.world.color=(.15,.10,.06)
if s.world.use_nodes:s.world.node_tree.nodes.get('Background').inputs[0].default_value=(.20,.13,.07,1);s.world.node_tree.nodes.get('Background').inputs[1].default_value=.3
# Light actual model cathodes, never substitute text planes.
orange=bpy.data.materials.new('Orange discharge');orange.use_nodes=True;nt=orange.node_tree;nt.nodes.clear();e=nt.nodes.new('ShaderNodeEmission');e.inputs['Color'].default_value=(1,.045,.001,1);e.inputs['Strength'].default_value=35;out=nt.nodes.new('ShaderNodeOutputMaterial');nt.links.new(e.outputs[0],out.inputs['Surface'])
dark=mat('Unlit electrode',(.025,.03,.032),.6,.45)
import re
for o in s.objects:
 if re.match('N[1-6]_',o.name) and ('digit' in o.name or 'Anode_square_mesh' in o.name):
  o.data=o.data.copy();o.data.materials.clear();o.data.materials.append(dark)
  m=re.fullmatch(r'N([1-6])_Cathode_digit_([0-9])',o.name)
  if m and '163045'[int(m[1])-1]==m[2]:
   o.data.materials[0]=orange;mod=o.modifiers.new('Orange discharge thickness','DISPLACE');mod.strength=.12;mod.mid_level=0
# Optical glow, broad and orange.
t=bpy.data.node_groups.new('Cafe optical bloom','CompositorNodeTree');t.interface.new_socket(name='Image',in_out='OUTPUT',socket_type='NodeSocketColor')
r=t.nodes.new('CompositorNodeRLayers');g=t.nodes.new('CompositorNodeGlare');g.inputs['Type'].default_value='Fog Glow';g.inputs['Quality'].default_value='High';g.inputs['Threshold'].default_value=.8;g.inputs['Strength'].default_value=4.5;g.inputs['Size'].default_value=.5
out=t.nodes.new('NodeGroupOutput');t.links.new(r.outputs['Image'],g.inputs['Image']);t.links.new(g.outputs['Image'],out.inputs['Image']);s.compositing_node_group=t
s.camera.location=(45,-1110,400);target=Vector((0,20,138));s.camera.rotation_euler=(target-s.camera.location).to_track_quat('-Z','Y').to_euler();s.camera.data.type='PERSP';s.camera.data.lens=48
s.camera.data.dof.use_dof=True;s.camera.data.dof.focus_distance=(s.camera.location-Vector((0,10,60))).length;s.camera.data.dof.aperture_fstop=8
s.render.resolution_x=2400;s.render.resolution_y=1704;s.render.resolution_percentage=100;s.render.film_transparent=False;s.render.image_settings.file_format='PNG';s.render.image_settings.color_mode='RGB'
s.cycles.samples=48;s.cycles.use_denoising=True;s.cycles.device='CPU';s.view_settings.view_transform='AgX';s.view_settings.look='AgX - Medium High Contrast';s.view_settings.exposure=0
s.render.filepath=str(P/'assets/cafe_scaled.png')
bpy.ops.wm.save_as_mainfile(filepath=str(P/'cafe_scene_scaled.blend'),compress=True)
bpy.ops.render.render(write_still=True)
(P/'assets/cafe_scale_manifest.json').write_text(json.dumps({'unit':'mm','clock_assembly':[260,80.1185436249,84],'clock_scaled':False,'cup':{'diameter':80,'height':90,'width_with_handle':105,'reference':'https://kinto-europe.com/products/27635','geometry':'generic dimensional approximation, not manufacturer CAD'},'books':{'A5':[148,210,18],'B6':[128,182,22]},'shelf':[1100,320,24]},indent=2))
