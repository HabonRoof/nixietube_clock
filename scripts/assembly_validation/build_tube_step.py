import sys,json,math
from pathlib import Path
sys.path.insert(0,'/Applications/FreeCAD.app/Contents/Resources/lib')
import FreeCAD as A,Part
P=Path(__file__).parent
source=Part.read(str(P.parent/'in4_tube_model/IN4_mechanical.step'))
b=json.loads((P/'boards.json').read_text());fits={f['ref']:f for f in json.loads((P/'tube_fit.json').read_text())}
for f in b['display_board_in4']['footprints']:
 if not f['ref'].startswith('N'):continue
 fit=fits[f['ref']];dx=fit['center'][0]-f['at'][0];dy=fit['center'][1]-f['at'][1];a=math.radians(f['at'][2]);x=math.cos(a)*dx-math.sin(a)*dy;y=-(math.sin(a)*dx+math.cos(a)*dy)
 # Bottom-view pin convention corrected by Y reflection for this provisional numeral datum.
 m=A.Matrix();m.A22=-1;sh=source.transformGeometry(m);sh.rotate(A.Vector(),A.Vector(0,0,1),-fit['angle_clockwise_deg']-f['at'][2]);sh.translate(A.Vector(x,y,8.5));sh.exportStep(str(P/'models'/('display_board_in4_'+f['ref']+'_tube.step')))
