"""Export assembly and separately positionable STL parts from the saved FreeCAD assembly."""
import sys,json,math
from pathlib import Path
sys.path.insert(0,'/Applications/FreeCAD.app/Contents/Resources/lib')
import FreeCAD as A,Part,Mesh,MeshPart
P=Path(__file__).resolve().parent
D=A.openDocument(str(P/'clock_assembly.FCStd'));root=D.getObject('Clock')
out=P/'stl_parts';out.mkdir(exist_ok=True)
# getShape includes child placements, preserving shared assembly coordinates.
shape=Part.getShape(root);bb=shape.BoundBox
assert abs(bb.XLength-250)<.1 and abs(bb.YLength-60)<.1 and abs(bb.ZLength-63.5)<.1,bb
pieces=[D.getObject('OUTBOXX_B'),D.getObject('OUTBOXX_F'),D.getObject('main_board'),D.getObject('display_board')]
pieces += list(D.getObject('IN4_tubes_6').Group)+list(D.getObject('Metal_sockets_84').Group)
full=Mesh.Mesh();report=[]
for obj in pieces:
 # Global placement is needed when exporting a subgroup by itself.
 sh=Part.getShape(obj);local=obj.Placement
 globalp=obj.getGlobalPlacement()
 sh.Placement=globalp.multiply(local.inverse()).multiply(sh.Placement)
 mesh=MeshPart.meshFromShape(Shape=sh,LinearDeflection=.05,AngularDeflection=math.radians(15),Relative=False)
 path=out/(obj.Name+'.stl');mesh.write(str(path));full.addMesh(mesh)
 report.append(dict(name=obj.Name,facets=mesh.CountFacets,file=path.name))
path=P/'clock_assembly.stl';full.write(str(path));check=Mesh.Mesh(str(path));b=check.BoundBox
assert abs(b.XLength-250)<.1 and abs(b.YLength-60)<.1 and abs(b.ZLength-63.5)<.1,b
(P/'stl_export_validation.json').write_text(json.dumps(dict(units='mm (STL itself is unitless)',combined_facets=check.CountFacets,bounds_mm=[b.XMin,b.YMin,b.ZMin,b.XMax,b.YMax,b.ZMax],parts=report),indent=2))
print('STL exported',path,check.CountFacets,'triangles',b,flush=True)
