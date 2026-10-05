from pathlib import Path
import zipfile,json,struct
R=Path(__file__).resolve().parents[3];src=R/'output/pptx/nixie_clock_booklet_zh_TW_lineart_3d.pptx';dst=R/'tmp/booklet-v2/candidate-textured.pptx'
models={}
for n in ['front_shell','rear_cover','tilt_base_15deg']:
 b=(R/'doc/user_guide/booklet/assets/models_textured'/f'{n}.glb').read_bytes();l=struct.unpack_from('<I',b,12)[0];d=json.loads(b[20:20+l]);assert d['materials'][0]['pbrMetallicRoughness']['baseColorTexture'];assert all('bufferView' in v for v in d['images']);assert all('TEXCOORD_0' in p['attributes'] for m in d['meshes'] for p in m['primitives']);models[f'ppt/media/{n}.glb']=b;print(n,'embedded texture',len(b))
with zipfile.ZipFile(src) as z,zipfile.ZipFile(dst,'w',zipfile.ZIP_DEFLATED) as w:
 for n in z.namelist():w.writestr(n,models.get(n,z.read(n)))
with zipfile.ZipFile(src) as a,zipfile.ZipFile(dst) as b:
 changed=[n for n in a.namelist() if a.read(n)!=b.read(n)];assert set(changed)==set(models);print('Changed parts only:',changed)
