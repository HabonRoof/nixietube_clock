"""Bake source procedural wood into embedded GLB base-color textures."""
import bpy,json
from pathlib import Path
from mathutils import Vector,Matrix
R=Path(__file__).resolve().parents[3];out=R/'doc/user_guide/booklet/assets/models_textured';out.mkdir(exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(R/'hardware/case/revision_h/clock_case_revision_h.blend'))
s=bpy.context.scene;s.render.engine='CYCLES';s.cycles.device='CPU';s.cycles.samples=8
s.render.bake.use_pass_direct=False;s.render.bake.use_pass_indirect=False;s.render.bake.use_pass_color=True;s.render.bake.margin=16
for name in ['front_shell','rear_cover','tilt_base_15deg']:
 bpy.ops.object.select_all(action='DESELECT');o=bpy.data.objects[name];c=o.copy();c.data=o.data.copy();s.collection.objects.link(c);c.parent=None;c.matrix_world=o.matrix_world.copy();c.hide_viewport=False;c.hide_render=False;c.hide_set(False);c.select_set(True);bpy.context.view_layer.objects.active=c
 bpy.ops.object.convert(target='MESH');c=bpy.context.object
 for slot in c.material_slots:
  if slot.material:slot.material=slot.material.copy()
 bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project(angle_limit=1.1519,island_margin=.02);bpy.ops.object.mode_set(mode='OBJECT')
 im=bpy.data.images.new(name+'_wood_basecolor',width=2048,height=2048,alpha=False);im.colorspace_settings.name='sRGB'
 for mat in c.data.materials:
  mat.use_nodes=True;node=mat.node_tree.nodes.new('ShaderNodeTexImage');node.image=im;mat.node_tree.nodes.active=node
 print('BAKING',name,flush=True);bpy.ops.object.bake(type='DIFFUSE')
 im.filepath_raw=str(out/(name+'_basecolor.png'));im.file_format='PNG';im.save();im.pack()
 mat=bpy.data.materials.new(name+'_baked_wood');mat.use_nodes=True;bsdf=mat.node_tree.nodes.get('Principled BSDF');bsdf.inputs['Metallic'].default_value=0;bsdf.inputs['Roughness'].default_value=.4
 tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=im;mat.node_tree.links.new(tex.outputs['Color'],bsdf.inputs['Base Color'])
 c.data.materials.clear();c.data.materials.append(mat)
 for poly in c.data.polygons:poly.material_index=0
 pts=[c.matrix_world@Vector(v) for v in c.bound_box];lo=Vector([min(p[i] for p in pts) for i in range(3)]);hi=Vector([max(p[i] for p in pts) for i in range(3)]);center=(lo+hi)/2;scale=2/max(hi-lo)
 view=Vector((260,-450,280)).to_track_quat('Z','Y').to_matrix().transposed().to_4x4();c.matrix_world=view@Matrix.Scale(scale,4)@Matrix.Translation(-center)@c.matrix_world
 bpy.ops.export_scene.gltf(filepath=str(out/(name+'.glb')),export_format='GLB',use_selection=True,export_yup=False,export_materials='EXPORT',export_cameras=False,export_lights=False)
 bpy.data.objects.remove(c,do_unlink=True);print('DONE',name,flush=True)
