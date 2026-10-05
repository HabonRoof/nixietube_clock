import sys,json
from pathlib import Path
sys.path.append('/Applications/FreeCAD.app/Contents/Resources/lib')
import FreeCAD as App,Part
p=Path(__file__).resolve().parent
s=Part.Shape(); s.read(str(p/'IN4_detailed.step'))
print('STEP',s.isValid(),s.BoundBox,flush=True)
print('Bounding methods',[x for x in dir(s) if 'bound' in x.lower()],flush=True)
print('Optimal',s.optimalBoundingBox(),flush=True)
d=App.openDocument(str(p/'IN4_nominal.FCStd'))
for o in d.Objects:
    b=o.Shape.BoundBox
    if b.XMin < -15.01 or b.XMax>15.01 or b.YMin< -15.01 or b.YMax>15.01 or b.ZMin < -10.01 or b.ZMax>31.01:
        print(o.Label,b,flush=True)

print("Volumes",sum(o.Shape.Volume for o in d.Objects),s.Volume,"solids",len(s.Solids),flush=True)
