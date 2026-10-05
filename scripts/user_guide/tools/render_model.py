import bpy,json
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view
from pathlib import Path
out=Path(__file__).resolve().parents[3] / 'doc/user_guide/assets'
s=bpy.context.scene
s.render.resolution_x=1600;s.render.resolution_y=1000;s.render.resolution_percentage=100
s.cycles.samples=32;s.cycles.use_denoising=True;s.cycles.device='CPU'
s.render.image_settings.file_format='PNG'
print('OUT',out)
for n in ['front_shell','rear_cover','BTN0_button','BTN1_button','BTN2_button','USB_measured_reference']:
 o=bpy.data.objects[n];center=sum((o.matrix_world@Vector(c) for c in o.bound_box),Vector())/8
 print('CENTER',n,tuple(center))
for name in ['hero','rear']:
 if name=='rear':
  s.camera.location=(380,540,220)
  s.camera.data.lens=58
  s.camera.rotation_euler=(Vector((125,0,50))-s.camera.location).to_track_quat('-Z','Y').to_euler()
 s.render.filepath=str(out/(name+'.png'))
 bpy.ops.render.render(write_still=True)
 points={}
 for n in ['BTN0_button','BTN1_button','BTN2_button','USB_measured_reference']:
  o=bpy.data.objects[n];center=sum((o.matrix_world@Vector(c) for c in o.bound_box),Vector())/8
  p=world_to_camera_view(s,s.camera,center);points[n]=[p.x,1-p.y]
 (out/(name+'_points.json')).write_text(json.dumps(points))
