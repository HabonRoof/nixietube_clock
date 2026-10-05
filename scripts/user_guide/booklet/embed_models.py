from pathlib import Path
from lxml import etree as E
from copy import deepcopy
import zipfile
R=Path(__file__).resolve().parents[3];T=R/'tmp/booklet-v2';A=R/'doc/user_guide/booklet/assets'
ns={'p':'http://schemas.openxmlformats.org/presentationml/2006/main','a':'http://schemas.openxmlformats.org/drawingml/2006/main','r':'http://schemas.openxmlformats.org/officeDocument/2006/relationships','mc':'http://schemas.openxmlformats.org/markup-compatibility/2006','am3d':'http://schemas.microsoft.com/office/drawing/2017/model3d'}
q=lambda prefix,name:'{'+ns[prefix]+'}'+name
oracle=E.fromstring(zipfile.ZipFile(T/'model3d.pptx').read('ppt/slides/slide1.xml')).find('.//mc:AlternateContent',ns)
z=zipfile.ZipFile(T/'candidate.pptx');data={n:z.read(n) for n in z.namelist()}
models={n:n.removeprefix('part_') for n in ['part_front_shell','part_rear_cover','part_tilt_base_15deg','part_BTN0_button','part_BTN1_button','part_BTN2_button','main_board','display_board','in4_tube_socket','fasteners','tubes','spacers','screws']}
records=[]
for i in [25,27,29,30]:
 fn=f'ppt/slides/slide{i}.xml';root=E.fromstring(data[fn]);relfn=f'ppt/slides/_rels/slide{i}.xml.rels';rels=E.fromstring(data[relfn])
 order={25:['part_front_shell','part_tilt_base_15deg','tubes','display_board','spacers','main_board','part_rear_cover','screws','part_BTN0_button','part_BTN1_button','part_BTN2_button'],27:['part_front_shell','part_rear_cover','part_tilt_base_15deg'],29:['main_board','display_board'],30:['in4_tube_socket','fasteners']}[i]
 pics=list(root.findall('.//p:pic',ns));assert len(pics)==len(order)
 for idx,pic in enumerate(pics):
  pr=pic.find('p:nvPicPr/p:cNvPr',ns);name=order[idx];
  if name not in models:continue
  model=models[name];alt=deepcopy(oracle);frame=alt.find('.//p:graphicFrame',ns);modelnode=frame.find('.//am3d:model3d',ns);rid='rIdModel'+str(len(records)+1)
  frame.find('p:nvGraphicFramePr/p:cNvPr',ns).attrib.update({'id':pr.get('id'),'name':'3D '+model})
  xfrm=pic.find('p:spPr/a:xfrm',ns);f=frame.find('p:xfrm',ns)
  for child in list(f):f.remove(child)
  for child in xfrm:f.append(deepcopy(child))
  ext=xfrm.find('a:ext',ns);modelnode.find('am3d:spPr/a:xfrm/a:ext',ns).attrib.update(ext.attrib)
  modelnode.set(q('r','embed'),rid)
  imageid=pic.find('p:blipFill/a:blip',ns).get(q('r','embed'))
  modelnode.find('am3d:raster/am3d:blip',ns).set(q('r','embed'),imageid)
  modelnode.find('am3d:trans/am3d:meterPerModelUnit',ns).set('n','1000000')
  modelnode.find('am3d:camera/am3d:pos',ns).set('z','180000000')
  modelnode.find('am3d:objViewport',ns).set('viewportSz',ext.get('cy'))
  fallback=alt.find('mc:Fallback',ns)
  for child in list(fallback):fallback.remove(child)
  fallback.append(deepcopy(pic));pic.getparent().replace(pic,alt)
  E.SubElement(rels,'{http://schemas.openxmlformats.org/package/2006/relationships}Relationship',Id=rid,Type='http://schemas.microsoft.com/office/2017/06/relationships/model3d',Target='../media/'+model+'.glb')
  data['ppt/media/'+model+'.glb']=(A/'models'/(model+'.glb')).read_bytes();records.append([i,name,model])
 data[fn]=E.tostring(root,xml_declaration=True,encoding='UTF-8',standalone=True);data[relfn]=E.tostring(rels,xml_declaration=True,encoding='UTF-8',standalone=True)
ct=E.fromstring(data['[Content_Types].xml']);E.SubElement(ct,'{http://schemas.openxmlformats.org/package/2006/content-types}Default',Extension='glb',ContentType='model/gltf-binary');data['[Content_Types].xml']=E.tostring(ct,xml_declaration=True,encoding='UTF-8',standalone=True)
with zipfile.ZipFile(T/'candidate-3d.pptx','w',zipfile.ZIP_DEFLATED) as out:
 for n,b in data.items():out.writestr(n,b)
print(records)
