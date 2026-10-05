import bpy,json
from pathlib import Path
from mathutils import Vector
p=Path(__file__).resolve().parent
bpy.ops.wm.open_mainfile(filepath=str(p/'IN4_nominal.blend'))
scene=bpy.context.scene
objects=[o for o in scene.objects if o.type=='MESH']
assert len(objects)==59
expected=['1','7','5','9','3','4','0','6','2','8']
digits=sorted([o for o in objects if o.name.startswith('Cathode_digit_')],key=lambda o:o.location.z)
assert [o.name[-1] for o in digits]==expected
for i,o in enumerate(digits):
    assert o['layer_from_bottom']==i+1
    assert abs(o.location.z-(10+i*1.65))<.0001
assert bpy.data.scenes.get('Digit layers - isolated inspection') is not None
assert scene.unit_settings.scale_length == .001 or abs(scene.unit_settings.scale_length-.001)<1e-9
points=[o.matrix_world@Vector(v) for o in objects for v in o.bound_box]
dimensions=[max(v[i] for v in points)-min(v[i] for v in points) for i in range(3)]
assert all(abs(a-b)<.08 for a,b in zip(dimensions,[30,30,41]))
report=dict(objects=len(objects),units='mm',unit_scale=scene.unit_settings.scale_length,bounds_mm=dimensions,digit_order_bottom_to_top=expected)
(p/'blender_validation.json').write_text(json.dumps(report,indent=2))
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(p/'IN4_nominal.blend'),compress=True)
print(report)
