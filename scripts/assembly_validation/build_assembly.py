import bpy,json,math,re
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
P=Path(__file__).parent;ROOT=P.parents[1]
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'scripts/in4_tube_model/IN4_nominal.blend'));s=bpy.context.scene
sources=[o for o in s.objects if o.type=='MESH']
for o in list(s.objects):
 if o.type in ['LIGHT','CAMERA']:bpy.data.objects.remove(o,do_unlink=True)
fits=json.loads((P/'tube_fit.json').read_text());boards=json.loads((P/'boards.json').read_text());groups={};tube_objs=[]
def col(name):
 c=bpy.data.collections.new(name);s.collection.children.link(c);return c
def mat(name,c,metal=0):
 m=bpy.data.materials.new(name);m.diffuse_color=(*c,1);m.use_nodes=True;b=m.node_tree.nodes.get('Principled BSDF');b.inputs['Base Color'].default_value=m.diffuse_color;b.inputs['Metallic'].default_value=metal;b.inputs['Roughness'].default_value=.3;return m
orange=mat('Enclosure orange',(.7,.3,.08));gold=mat('Socket brass',(.6,.38,.09),.8)
for fit in fits:
 c=col(fit['ref']+' nominal tube - provisional upright datum');a=math.radians(fit['angle_clockwise_deg']);cx,cy=fit['center']
 T=Matrix.Translation((cx-55,cy-121,24))@Matrix.Rotation(a,4,'Z')@Matrix.Rotation(math.pi,4,'X')
 for src in sources:
  o=src.copy();o.data=src.data;c.objects.link(o)
  correction=Matrix.Diagonal((1,-1,1,1)) if src.name.startswith(('Pin_','Feedthrough_')) else Matrix.Identity(4)
  o.matrix_world=T@correction@src.matrix_world;o.hide_render=False;o.name=fit['ref']+'_'+src.name;tube_objs.append(o)
for o in sources:bpy.data.objects.remove(o,do_unlink=True)
case=[]
for part in ['B','F']:
 bpy.ops.wm.stl_import(filepath=str(ROOT/'hardware/case'/('OUTBOXX_'+part+'.STL')));o=bpy.context.object;o.name='Case_'+part;o.data.materials.clear();o.data.materials.append(orange);case.append(o)
# Board z=0 is back copper plane; KiCad front is z=+1.6. Assembly front faces towards -case Z.
# Model-based connector housing contact: 2.0 mm header body + 4.635 mm socket = 6.635 mm air gap.
placements={'display':(-55,-121,34.1),'main':(-21,-91,42.335)}
for n in ['display','main']:
 bpy.ops.object.select_all(action='DESELECT');bpy.ops.import_scene.gltf(filepath=str(P/(n+'_completed.glb')))
 objs=list(bpy.context.selected_objects);allmat={o:o.matrix_world.copy() for o in objs};dx,dy,z=placements[n]
 T=Matrix.Translation((dx,dy,z))@Matrix.Diagonal((1,-1,-1,1));arr=[]
 for o in objs:
  if o.type!='MESH':continue
  if re.match(r'N[1-6](\.|$)',o.name):bpy.data.objects.remove(o,do_unlink=True);continue
  o.parent=None;o.matrix_world=T@allmat[o];o.name=n+'_'+o.name;arr.append(o)
 groups[n]=arr
# Use CAD-generated socket arrays directly, guaranteeing pad-based placement.
meshdata=json.loads((P/'generated_meshes.json').read_text());socket_objs=[]
for f in boards['display_board_in4']['footprints']:
 if not re.fullmatch('N[1-6]',f['ref']):continue
 T=Matrix.Translation((f['at'][0]-55,f['at'][1]-121,32.5))@Matrix.Rotation(math.radians(-f['at'][2]),4,'Z')@Matrix.Rotation(math.pi,4,'X')
 for d in meshdata:
  if d['model']!='display_board_in4_'+f['ref']+'_sockets.step':continue
  mesh=bpy.data.meshes.new('socket');mesh.from_pydata(d['vertices'],[],d['faces']);mesh.update();o=bpy.data.objects.new(f['ref']+'_socket_'+str(d['part']+1),mesh);s.collection.objects.link(o);o.matrix_world=T;o.data.materials.append(gold);socket_objs.append(o)
# World-space mesh intersection checks. Triangle crossings are not a signed minimum-clearance certification.
def bvh(o):
 return BVHTree.FromPolygons([o.matrix_world@v.co for v in o.data.vertices],[tuple(p.vertices) for p in o.data.polygons])
caseb=[bvh(o) for o in case];hits=[]
for o in groups['main']+groups['display']+socket_objs:
 tree=bvh(o)
 for c,cb in zip(case,caseb):
  if tree.overlap(cb):hits.append([o.name,c.name])
# Probe rear internal plane along Z at high component centers.
probes=[]
for o in groups['main']:
 if any(k in o.name for k in ['U9','U6','C34','J1']):
  pts=[o.matrix_world@Vector(v) for v in o.bound_box];center=sum(pts,Vector())/8
  hit=caseb[1].ray_cast(Vector((center.x,center.y,43)),Vector((0,0,1)))
  probes.append(dict(object=o.name,z_bounds=[min(p.z for p in pts),max(p.z for p in pts)],rear_inner_z=hit[0].z if hit[0] else None,clearance=(hit[0].z-max(p.z for p in pts)) if hit[0] else None))
(P/'collision_report.json').write_text(json.dumps(dict(case_surface_intersections=hits,rear_probes=probes,display_front_z=32.5,main_front_z=40.735,board_gap=6.635,socket_tail_z=40.3,nominal_socket_tail_to_main_front=.435,notes='Axial stack is estimated, not measured; generated package heights are provisional.'),indent=2))
def aim(o,p):
 back=(o.location-Vector(p)).normalized();right=Vector((0,-1,0)).cross(back).normalized();up=back.cross(right);o.rotation_euler=Matrix((right,up,back)).transposed().to_euler()
for loc,power,size in [((30,-110,-150),1400000,170),((270,30,-70),900000,120),((70,100,50),1000000,100)]:
 bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.data.energy=power;o.data.size=size;aim(o,(125,30,20))
s.world.use_nodes=True;s.world.node_tree.nodes['Background'].inputs[0].default_value=(.16,.19,.23,1);s.world.node_tree.nodes['Background'].inputs[1].default_value=.65
bpy.ops.object.camera_add();c=bpy.context.object;c.data.type='ORTHO';c.data.clip_end=5000;s.camera=c
s.render.engine='CYCLES';s.cycles.samples=24;s.cycles.use_denoising=True;s.cycles.max_bounces=12;s.cycles.transmission_bounces=8;s.render.resolution_x=1800;s.render.resolution_y=1000;s.render.resolution_percentage=100;s.render.image_settings.file_format='PNG';s.view_settings.view_transform='AgX'
def render(name,pos,target,scale):
 c.location=pos;aim(c,target);c.data.ortho_scale=scale;s.render.filepath=str(P/(name+'.png'));bpy.ops.render.render(write_still=True)
c.location=(425,-124,-282);aim(c,(125,30,18));c.data.ortho_scale=310
s['validation']='See README.md. Nominal tube numeral-to-pin datum not physically confirmed.'
bpy.context.preferences.filepaths.save_version=0;bpy.ops.wm.save_as_mainfile(filepath=str(P/'clock_with_boards.blend'),compress=True)
render('assembled_45deg',(425,-124,-282),(125,30,18),310)
for o in case:o.hide_render=True
render('internal_45deg',(380,-150,-260),(125,30,28),295)
# Separate layers along case depth for inspection, without altering saved true-stack scene.
for o in tube_objs:o.location.z-=30
for o in groups['main']:o.location.z+=35
render('exploded',(370,-170,-180),(125,30,30),320)
for o in tube_objs:o.location.z+=30
for o in groups['main']:o.location.z-=35
# Close-up socket interface with glass/tube removed; pad geometry and socket bores visible.
for o in tube_objs:o.hide_render=True
render('socket_detail',(85,-15,-35),(71,30,34),55)
print('Assembly complete',hits,probes)
