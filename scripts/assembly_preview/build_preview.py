"""Render the original case STL meshes with six existing nominal IN-4 models (mm)."""
import bpy, math, json
from pathlib import Path
from mathutils import Vector, Matrix
ROOT=Path(__file__).resolve().parents[2]; OUT=Path(__file__).resolve().parent
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'scripts/in4_tube_model/IN4_nominal.blend'))
s=bpy.context.scene
sources=[o for o in s.objects if o.type=='MESH']
# Original case face is z=0, rear cover ends at z=56.5.
# Tube base at z=24 gives 7 mm face protrusion, estimated from supplied side photographs.
for i in range(6):
 col=bpy.data.collections.new('IN4_%d'%(i+1));s.collection.children.link(col)
 T=Matrix.Translation((35+36*i,30,24)) @ Matrix.Rotation(math.pi,4,'X')
 for src in sources:
  obj=src.copy();obj.data=src.data;col.objects.link(obj);obj.matrix_world=T@src.matrix_world;obj.hide_render=False
for src in sources:bpy.data.objects.remove(src,do_unlink=True)
for o in list(s.objects):
 if o.type in {'LIGHT','CAMERA'}:bpy.data.objects.remove(o,do_unlink=True)
m=bpy.data.materials.new('Warm orange printed enclosure');m.diffuse_color=(.72,.32,.085,1);m.use_nodes=True
bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=m.diffuse_color;bs.inputs['Roughness'].default_value=.36
for part in ['B','F']:
 bpy.ops.wm.stl_import(filepath=str(ROOT/'hardware/case'/('OUTBOXX_'+part+'.STL')))
 o=bpy.context.object;o.name='Original enclosure '+part;o.data.materials.clear();o.data.materials.append(m)
 # Preserve supplied mesh geometry.
def aim(o,p):
 back=(o.location-Vector(p)).normalized();right=Vector((0,-1,0)).cross(back).normalized();up=back.cross(right);o.rotation_euler=Matrix((right,up,back)).transposed().to_euler()
for name,loc,power,size in [('Key',(35,-130,-160),1600000,180),('Fill',(270,40,-100),1000000,140),('Rim',(80,100,10),1200000,110)]:
 bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.name=name;o.data.energy=power;o.data.shape='DISK';o.data.size=size;aim(o,(125,30,15))
s.world.use_nodes=True;s.world.node_tree.nodes['Background'].inputs[0].default_value=(.19,.22,.27,1);s.world.node_tree.nodes['Background'].inputs[1].default_value=.5
bpy.ops.object.camera_add();c=bpy.context.object;c.name='Assembly camera';c.data.type='ORTHO';c.data.clip_end=5000;s.camera=c
s.render.engine='CYCLES';s.cycles.samples=32;s.cycles.use_denoising=True;s.cycles.max_bounces=12;s.cycles.transmission_bounces=8
s.render.resolution_x=1800;s.render.resolution_y=850;s.render.resolution_percentage=100;s.render.image_settings.file_format='PNG';s.view_settings.view_transform='AgX'
s['assembly_notes']='Original STL geometry; six nominal IN4 tubes at 36 mm pitch. Tube axial placement estimated from photos; no PCB/connector fit verification.'
s['tube_base_case_z_mm']=24
for name,pos,scale in [('front',(125,30,-450),280),('45deg',(345,-100,-240),310)]:
 c.location=pos;aim(c,(125,30,18));c.data.ortho_scale=scale
 s.render.filepath=str(OUT/(name+'.png'))
 if name=='45deg':
  # Exact 45 degree azimuth from frontal direction, with 20 degree elevation.
  c.location=Vector((125,30,18))+Vector((300,-154.4,-300));aim(c,(125,30,18))
  bpy.context.preferences.filepaths.save_version=0;bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'clock_assembly.blend'),compress=True)
 bpy.ops.render.render(write_still=True)
(OUT/'README.md').write_text('''# 外殼與六管組裝預覽\n\n直接使用 OUTBOXX_B.STL、OUTBOXX_F.STL 與既有 IN4_nominal.blend。原始外殼網格未修改。六管中心為 (35+36i,30) mm。\n\n管底設在外殼座標 z=24 mm，玻璃正面突出約 7 mm；此軸向位置依照片估計。IN-4 本身為既有近似模型。此次為外觀組裝，未加入 PCB 或驗證連接器高度／內部干涉。未加入手鑽孔、螺絲或外加按鈕。\n\nfront.png：正面正交平視。45deg.png：水平偏轉45度、俯角約20度。clock_assembly.blend 可繼續編輯。\n''')
