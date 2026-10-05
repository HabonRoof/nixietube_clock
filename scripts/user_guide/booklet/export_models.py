import bpy,json
from pathlib import Path
from mathutils import Vector,Matrix
R=Path(__file__).resolve().parents[3];out=R/'doc/user_guide/booklet/assets/models'
bpy.ops.wm.open_mainfile(filepath=str(R/'hardware/case/revision_h/clock_case_revision_h.blend'))
s=bpy.context.scene
sets={n:[bpy.data.objects[n]] for n in ['front_shell','rear_cover','tilt_base_15deg','BTN0_button','BTN1_button','BTN2_button']}
for k,g in [('main_board','MAIN_BOARD_MOVE'),('display_board','DISPLAY_BOARD_MOVE'),('tubes','NIXIE_TUBES_MOVE'),('spacers','PCB_SPACERS_MOVE'),('screws','SCREWS_MOVE')]:sets[k]=[o for o in bpy.data.objects[g].children_recursive if o.type=='MESH' and not o.hide_render]
sets['in4_tube_socket']=[o for o in sets['tubes'] if o.name.startswith('N1_')]
sets['fasteners']=sets['spacers']+sets['screws']
for name,objs in sets.items():
 bpy.ops.object.select_all(action='DESELECT');copies=[]
 for o in objs:
  c=o.copy();c.data=o.data.copy();s.collection.objects.link(c);c.parent=None;c.matrix_world=o.matrix_world.copy();c.hide_viewport=False;c.hide_render=False;c.hide_set(False);copies.append(c)
 pts=[o.matrix_world@Vector(v) for o in copies for v in o.bound_box];lo=Vector([min(p[i] for p in pts) for i in range(3)]);hi=Vector([max(p[i] for p in pts) for i in range(3)]);center=(lo+hi)/2;scale=2/max(hi-lo)
 # Center and normalize each reusable part; bake the desired view into mesh geometry.
 view=Vector((260,-450,280)).to_track_quat('Z','Y').to_matrix().transposed().to_4x4()
 for c in copies:
  c.matrix_world=view@Matrix.Scale(scale,4)@Matrix.Translation(-center)@c.matrix_world;c.select_set(True)
 bpy.context.view_layer.objects.active=copies[0]
 bpy.ops.export_scene.gltf(filepath=str(out/(name+'.glb')),export_format='GLB',use_selection=True,export_yup=False,export_materials='EXPORT',export_cameras=False,export_lights=False)
 for c in copies:bpy.data.objects.remove(c,do_unlink=True)
 print('EXPORTED',name,flush=True)
