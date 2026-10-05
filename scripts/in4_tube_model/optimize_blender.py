import bpy,json,sys,time,shutil
from pathlib import Path
OUT=Path(__file__).resolve().parent
sys.path.insert(0,str(OUT))
from simplify_meshes import simplify_scene
path=OUT/'IN4_nominal.blend'
backup=OUT/'IN4_before_simplification.blend'
if not backup.exists(): shutil.copy2(path,backup)
bpy.ops.wm.open_mainfile(filepath=str(backup))
scene=bpy.context.scene
scene.render.filepath=str(OUT/'IN4_before_simplification.png')
t=time.perf_counter(); bpy.ops.render.render(write_still=True)
before_seconds=time.perf_counter()-t
records=simplify_scene(scene)
scene['mesh_optimization']='Reduced render meshes; STEP retains original CAD solids'
scene.render.filepath=str(OUT/'IN4_preview.png')
t=time.perf_counter(); bpy.ops.render.render(write_still=True)
after_seconds=time.perf_counter()-t
scene.cycles.samples=16; scene.cycles.use_denoising=True
t=time.perf_counter(); bpy.ops.render.render(write_still=True)
fast_seconds=time.perf_counter()-t
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(path),compress=True)
inspection=bpy.data.scenes['Digit layers - isolated inspection']
inspection.render.filepath=str(OUT/'IN4_digit_layers.png')
bpy.ops.render.render(write_still=True,scene=inspection.name)
report=dict(before_faces=sum(r['before_faces'] for r in records),after_faces=sum(r['after_faces'] for r in records),
            before_render_seconds=before_seconds,after_render_seconds=after_seconds,
            default_samples=16,default_render_seconds=fast_seconds,
            benchmark_note='Single consecutive CPU renders; same scene, resolution and samples. Second run may benefit from warm caches.',objects=records)
(OUT/'mesh_optimization.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k!='objects'}),flush=True)
