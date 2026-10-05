"""Revision H model renders for the editable booklet; original blend is read only."""
import bpy,json,re,sys,math
from pathlib import Path
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view
P=(Path(__file__).resolve().parents[3]/'doc/user_guide/booklet'); OUT=P/'assets';SRC=P.parents[2]/'hardware/case/revision_h/clock_case_revision_h.blend'
bpy.ops.wm.open_mainfile(filepath=str(SRC));s=bpy.context.scene
s.render.resolution_x=1600;s.render.resolution_y=1200;s.render.resolution_percentage=100
s.render.film_transparent=True;s.render.image_settings.file_format='PNG';s.render.image_settings.color_mode='RGBA'
s.cycles.samples=24;s.cycles.use_denoising=True;s.cycles.device='CPU'
s.view_settings.view_transform='Standard';s.view_settings.look='None';s.view_settings.exposure=-.6
bpy.data.objects['Studio_tabletop'].hide_render=True
s.camera.data.type='ORTHO'
original={o.name:o.matrix_world.copy() for o in s.objects}
base_visibility={o.name:o.hide_render for o in s.objects}
def visible_meshes():return [o for o in s.objects if o.type=='MESH' and not o.hide_render and o.visible_get()]
def frame(direction,margin=1.18):
 points=[o.matrix_world@Vector(c) for o in visible_meshes() for c in o.bound_box]
 mins=Vector([min(p[i] for p in points) for i in range(3)]);maxs=Vector([max(p[i] for p in points) for i in range(3)])
 center=(mins+maxs)/2;s.camera.location=center+Vector(direction);s.camera.rotation_euler=(-Vector(direction)).to_track_quat('-Z','Y').to_euler()
 rot=s.camera.rotation_euler.to_matrix().transposed();rp=[rot@(p-center) for p in points]
 width=max(p.x for p in rp)-min(p.x for p in rp);height=max(p.y for p in rp)-min(p.y for p in rp)
 aspect=s.render.resolution_x/s.render.resolution_y
 shift=Vector(((min(p.x for p in rp)+max(p.x for p in rp))/2,(min(p.y for p in rp)+max(p.y for p in rp))/2,0))
 s.camera.location+=s.camera.rotation_euler.to_matrix()@shift
 s.camera.data.ortho_scale=(max(width,height*aspect) if aspect>=1 else max(width/aspect,height))*margin
 bpy.context.view_layer.update()
def render(name):
 s.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
def reset():
 for o in s.objects:
  if o.type=='MESH':o.hide_render=base_visibility[o.name]
 # Parent roots then child transforms are restored in depth order.
 def depth(o):
  n=0
  while o.parent:n+=1;o=o.parent
  return n
 for o in sorted(s.objects,key=depth):
  if o.name in original:o.matrix_world=original[o.name]
 bpy.context.view_layer.update()
def select(names):
 names=set(names)
 for o in s.objects:
  if o.type=='MESH':o.hide_render=o.name not in names
 bpy.context.view_layer.update()
def group(name):return [o.name for o in bpy.data.objects[name].children_recursive if o.type=='MESH']
# Rear controls, original geometry.
frame((260,640,250),1.12);render('rear_controls')
pts={}
for n in ['BTN0_button','BTN1_button','BTN2_button','USB_measured_reference']:
 o=bpy.data.objects[n];center=sum([o.matrix_world@Vector(c) for c in o.bound_box],Vector())/8
 p=world_to_camera_view(s,s.camera,center);pts[n]=[p.x,1-p.y]
(OUT/'rear_controls_points.json').write_text(json.dumps(pts))
# Separate assembly layers, not an assembly instruction.
reset()
# All board/case layers translate along the original assembly normal.
axis=(bpy.data.objects['CLOCK_TILT_15deg'].matrix_world.to_3x3()@Vector((0,0,1))).normalized()
amounts={'FRONT_CASE_MOVE':-360,'NIXIE_TUBES_MOVE':-170,'DISPLAY_BOARD_MOVE':0,'PCB_SPACERS_MOVE':160,'MAIN_BOARD_MOVE':300,'REAR_CASE_MOVE':520,'SCREWS_MOVE':710,'BTN0_MOVE':300,'BTN1_MOVE':300,'BTN2_MOVE':300}
shifts={n:axis*d for n,d in amounts.items()}
for n,d in shifts.items():
 o=bpy.data.objects[n];m=o.matrix_world.copy();m.translation+=d;o.matrix_world=m
m=bpy.data.objects['tilt_base_15deg'].matrix_world.copy();m.translation+=axis*-360+Vector((0,0,-40));bpy.data.objects['tilt_base_15deg'].matrix_world=m
s.render.resolution_x=1800;s.render.resolution_y=2000;bpy.context.view_layer.update();frame((500,-900,700),1.12)
# Roll the whole view, never rotate individual assembly components.
s.camera.rotation_euler.rotate_axis('Z',math.radians(-25))
# Refit the rolled camera in screen space.
rot=s.camera.rotation_euler.to_matrix().transposed()
rp=[rot@(o.matrix_world@Vector(c)-s.camera.location) for o in visible_meshes() for c in o.bound_box]
xs=[p.x for p in rp];ys=[p.y for p in rp]
s.camera.location+=s.camera.rotation_euler.to_matrix()@Vector(((min(xs)+max(xs))/2,(min(ys)+max(ys))/2,0))
s.camera.data.ortho_scale=max((max(xs)-min(xs))/.9,max(ys)-min(ys))*1.12
d=bpy.data.lights.new('Exploded softbox','AREA');d.energy=3000000;d.shape='DISK';d.size=1100
o=bpy.data.objects.new('Exploded softbox',d);s.collection.objects.link(o);o.location=s.camera.location;o.rotation_euler=s.camera.rotation_euler
bpy.context.view_layer.update();render('exploded')
pts={}
for n in shifts:
 o=bpy.data.objects[n];parts=[ch for ch in o.children_recursive if ch.type=='MESH' and not ch.hide_render]
 if not parts:continue
 bounds=[ch.matrix_world@Vector(c) for ch in parts for c in ch.bound_box]
 center=sum(bounds,Vector())/len(bounds);q=world_to_camera_view(s,s.camera,center);pts[n]=[q.x,1-q.y]
(OUT/'exploded_points.json').write_text(json.dumps(pts,indent=2))
if '--exploded-only' in sys.argv:sys.exit(0)
# Render all six print parts separately, with geometry isolated rather than edited.
reset();s.render.resolution_x=1200;s.render.resolution_y=700
for n in ['front_shell','rear_cover','tilt_base_15deg','BTN0_button','BTN1_button','BTN2_button']:
 select([n]);frame((260,-450,280),1.17);render('part_'+n)
# Main assembly subgroups.
reset();s.render.resolution_x=1400;s.render.resolution_y=850
for root,name in [('MAIN_BOARD_MOVE','main_board'),('DISPLAY_BOARD_MOVE','display_board')]:
 select(group(root));frame((230,-450,260),1.15);render(name)
reset();select([n for n in group('NIXIE_TUBES_MOVE') if n.startswith('N1_')]);s.render.resolution_x=1000;s.render.resolution_y=1200;frame((180,-450,200),1.12);render('in4_tube_socket')
reset();select(group('PCB_SPACERS_MOVE')+group('SCREWS_MOVE'));s.render.resolution_x=1400;s.render.resolution_y=700;frame((230,-450,280),1.15);render('fasteners')
print('HARDWARE RENDERS COMPLETE',flush=True)
