"""Render original Revision H cathode meshes; no source blend changes."""
import bpy,re,json
from pathlib import Path
from mathutils import Vector
out=Path(__file__).resolve().parents[3]/'doc/user_guide/booklet/assets'
s=bpy.context.scene
s.render.resolution_x=1800;s.render.resolution_y=760;s.render.resolution_percentage=100
s.cycles.samples=24;s.cycles.use_denoising=True;s.cycles.device='CPU'
s.render.image_settings.file_format='PNG'
s.render.image_settings.color_mode='RGBA'
s.render.film_transparent=True
bpy.data.objects['Studio_tabletop'].hide_render=True
s.camera.data.type='PERSP';s.camera.data.lens=50;s.camera.data.sensor_width=36;s.camera.data.shift_x=0;s.camera.data.shift_y=0;s.camera.data.dof.use_dof=False
s.camera.location=(175,-425,118)
s.camera.rotation_euler=(Vector((125,0,48))-s.camera.location).to_track_quat('-Z','Y').to_euler()
s.view_settings.view_transform='Standard';s.view_settings.look='None';s.view_settings.exposure=-0.7
orange=bpy.data.materials.new('Guide bright orange discharge');orange.use_nodes=True
nt=orange.node_tree;nt.nodes.clear();e=nt.nodes.new('ShaderNodeEmission');e.inputs['Color'].default_value=(1.0,0.16,0.004,1);e.inputs['Strength'].default_value=10
output=nt.nodes.new('ShaderNodeOutputMaterial');nt.links.new(e.outputs[0],output.inputs['Surface'])
dark=bpy.data.materials.new('Guide unlit electrodes');dark.use_nodes=True
p=dark.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(0.028,0.034,0.038,1);p.inputs['Metallic'].default_value=.45;p.inputs['Roughness'].default_value=.6
# Larger optical glow on the emission, preserving editable original model.
tree=bpy.data.node_groups.new('Booklet orange glow','CompositorNodeTree')
tree.interface.new_socket(name='Image',in_out='OUTPUT',socket_type='NodeSocketColor')
rl=tree.nodes.new('CompositorNodeRLayers');g=tree.nodes.new('CompositorNodeGlare')
g.inputs['Type'].default_value='Fog Glow';g.inputs['Quality'].default_value='High'
g.inputs['Threshold'].default_value=1.4;g.inputs['Strength'].default_value=1.8;g.inputs['Size'].default_value=.4
output_node=tree.nodes.new('NodeGroupOutput');tree.links.new(rl.outputs['Image'],g.inputs['Image']);tree.links.new(g.outputs['Image'],output_node.inputs['Image'])
s.compositing_node_group=tree
# Each tube may share mesh datablocks: isolate materials per object.
for o in bpy.data.objects:
 if re.match(r'N[1-6]_',o.name) and ('digit' in o.name or 'Anode_square_mesh' in o.name):
  o.data=o.data.copy();o.data.materials.clear();o.data.materials.append(dark)
scenes=[('mode_clock','163045'),('mode_date','260910'),('mode_pomodoro','002500'),('mode_divergence','482691'),('mode_cathode','555555')]
for name,digits in scenes:
 for o in bpy.data.objects:
  m=re.fullmatch(r'N([1-6])_Cathode_digit_([0-9])',o.name)
  if m:
   active=digits[int(m[1])-1]==m[2]
   o.data.materials[0]=orange if active else dark
   mod=o.modifiers.get('Discharge envelope') or o.modifiers.new('Discharge envelope','DISPLACE')
   mod.strength=0.10;mod.mid_level=0;mod.show_render=active
 s.render.filepath=str(out/(name+'.png'));bpy.ops.render.render(write_still=True)
 if name=='mode_clock' and '--preview-only' in __import__('sys').argv:break
(out/'mode_manifest.json').write_text(json.dumps(dict(scenes),indent=2))
