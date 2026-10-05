"""Run: blender --background --python build_blender.py"""
import bpy, json, math
import sys
from mathutils import Vector, Quaternion
from pathlib import Path
OUT=Path(__file__).resolve().parent
sys.path.insert(0,str(OUT))
from digit_profiles import ORDER
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene
scene.unit_settings.system='METRIC'; scene.unit_settings.scale_length=.001
scene.unit_settings.length_unit='MILLIMETERS'
def mat(name,color,metal=0,rough=.3):
    m=bpy.data.materials.new(name); m.diffuse_color=(*color,1); m.use_nodes=True
    s=m.node_tree.nodes.get('Principled BSDF'); s.inputs['Base Color'].default_value=(*color,1)
    s.inputs['Metallic'].default_value=metal; s.inputs['Roughness'].default_value=rough
    return m
mats={'metal':mat('Nickel electrodes',(.38,.40,.42),.82,.27),
      'darkmetal':mat('Anode shields',(.16,.18,.19),.8,.36),
      'pin':mat('Pin metal',(.32,.30,.25),.8,.28),
      'ceramic':mat('Ivory ceramic',(.78,.75,.59),0,.4),
      'mica':mat('Mica support',(.17,.14,.10),.2,.5),
      'seal':mat('Feedthrough seal',(.26,.09,.045),.35,.3),
      'glass':mat('Clear glass',(.96,.99,1),0,.055)}
bs=mats['glass'].node_tree.nodes.get('Principled BSDF')
bs.inputs['Transmission Weight'].default_value=1; bs.inputs['IOR'].default_value=1.47
collections={}
for item in json.loads((OUT/'geometry.json').read_text()):
    group=item['group']
    if group not in collections:
        collections[group]=bpy.data.collections.new(group); scene.collection.children.link(collections[group])
    mesh=bpy.data.meshes.new(item['name']); mesh.from_pydata(item['vertices'],[],item['faces']); mesh.update()
    obj=bpy.data.objects.new(item['name'],mesh)
    if group=='Digits':
        digit=item['name'][-1]; layer=ORDER.index(digit); z=10+layer*1.65
        sub=bpy.data.collections.new('%02d - Digit %s'%(layer+1,digit))
        collections[group].children.link(sub); sub.objects.link(obj)
        for vertex in mesh.vertices: vertex.co.z-=z
        obj.location.z=z
        obj['layer_from_bottom']=layer+1; obj['digit']=digit
    else:
        collections[group].objects.link(obj)
    obj.data.materials.append(mats[item['material']])
    for face in mesh.polygons: face.use_smooth=True
    obj['accuracy']='Nominal geometry; internal dimensions and glass profile approximated from photos'
    obj['source_units']='mm; 1 Blender unit = 1 mm'
    if group=='Envelope': obj.display_type='WIRE'
scene['model_status']='Nominal IN-4 coarse-grid reconstruction, not measured specimen'
scene['external_dimensions_mm']='30 diameter x 31 glass height; total 41 including tip'
scene['step_export']='IN4_detailed.step: matching CAD source solids, millimeters'
scene['digit_order_bottom_to_top']=', '.join(ORDER)
def aim(obj,point): obj.rotation_euler=(Vector(point)-obj.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(57,-75,65)); camera=bpy.context.object; camera.name='Camera_assembly'
aim(camera,(0,0,11)); camera.data.type='ORTHO'; camera.data.ortho_scale=60; scene.camera=camera
for name,loc,power,size in [('Key',(25,-35,65),180000,45),('Fill',(-40,-15,35),100000,35),('Rim',(0,40,45),230000,30)]:
    bpy.ops.object.light_add(type='AREA',location=loc); light=bpy.context.object; light.name=name
    light.data.energy=power; light.data.shape='DISK'; light.data.size=size; aim(light,(0,0,12))
scene.world.color=(.18,.18,.18)
scene.render.engine='CYCLES'; scene.cycles.samples=16; scene.cycles.use_denoising=True
scene.cycles.max_bounces=12; scene.cycles.transmission_bounces=8
scene.render.resolution_x=1100; scene.render.resolution_y=1100; scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
scene.view_settings.view_transform='AgX'
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            space=area.spaces.active; space.clip_end=10000; space.clip_start=.01
            space.region_3d.view_distance=75; space.region_3d.view_location=(0,0,12)
            space.region_3d.view_rotation=camera.rotation_euler.to_quaternion()
bpy.ops.object.select_all(action='DESELECT')
# Persist a second scene that shows every digit separately in physical layer order.
inspection=bpy.data.scenes.new('Digit layers - isolated inspection')
inspection.unit_settings.system='METRIC'; inspection.unit_settings.scale_length=.001
inspection.unit_settings.length_unit='MILLIMETERS'
inspection.world=bpy.data.worlds.new('Inspection dark background'); inspection.world.use_nodes=True
inspection.world.node_tree.nodes['Background'].inputs[0].default_value=(.012,.018,.026,1)
display=bpy.data.materials.new('Inspection copper'); display.use_nodes=True
nodes=display.node_tree.nodes; nodes.clear()
em=nodes.new('ShaderNodeEmission'); em.inputs[0].default_value=(1,.40,.10,1)
out=nodes.new('ShaderNodeOutputMaterial'); display.node_tree.links.new(em.outputs[0],out.inputs[0])
for i,digit in enumerate(ORDER):
    source=bpy.data.objects['Cathode_digit_'+digit]
    obj=source.copy(); obj.data=source.data.copy(); inspection.collection.objects.link(obj)
    x=(i%5-2)*14; y=14 if i<5 else -14
    obj.location=(x,y,0); obj.data.materials.clear(); obj.data.materials.append(display)
    font=bpy.data.curves.new('Layer label','FONT'); font.body='%02d  |  DIGIT %s'%(i+1,digit)
    font.align_x='CENTER'; font.size=1.15
    label=bpy.data.objects.new('Layer_%02d_label'%(i+1),font); inspection.collection.objects.link(label)
    label.location=(x,y-11,0); font.materials.append(display)
cam=bpy.data.objects.new('Inspection_camera',bpy.data.cameras.new('Inspection_camera'))
inspection.collection.objects.link(cam); cam.location=(0,0,80); cam.data.type='ORTHO'; cam.data.ortho_scale=76
inspection.camera=cam
inspection.render.engine='CYCLES'; inspection.cycles.samples=8
inspection.render.resolution_x=1400; inspection.render.resolution_y=1050; inspection.render.resolution_percentage=100
inspection.render.image_settings.file_format='PNG'
inspection.render.filepath=str(OUT/'IN4_digit_layers.png')
from simplify_meshes import simplify_scene
simplify_scene(scene)
scene['mesh_optimization']='Reduced render meshes; STEP retains original CAD solids'
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'IN4_nominal.blend'),compress=True)
bpy.ops.render.render(write_still=True,scene=inspection.name)
scene.render.filepath=str(OUT/'IN4_preview.png'); bpy.ops.render.render(write_still=True)
# Orthographic face with glass removed for inspection of grid and cathodes.
for obj in collections['Envelope'].objects: obj.hide_render=True
camera.location=(0,0,90); camera.rotation_euler=(0,0,0); camera.data.ortho_scale=33
scene.render.filepath=str(OUT/'IN4_front_inspection.png'); bpy.ops.render.render(write_still=True)
print('Blender model saved at true millimeter scene scale')
