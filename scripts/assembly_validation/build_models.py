import sys,json,math
from pathlib import Path
sys.path.insert(0,'/Applications/FreeCAD.app/Contents/Resources/lib')
import FreeCAD as A,Part
P=Path(__file__).parent;M=P/'models';M.mkdir(exist_ok=True)
V=A.Vector
specs=json.loads((P/'model_specs.json').read_text());meshes=[]
def box(x,y,z,dx,dy,dz):return Part.makeBox(dx,dy,dz,V(x,y,z))
def save(parts,path):
 Part.makeCompound([s for s,mat in parts]).exportStep(str(M/path))
 for i,(s,mat) in enumerate(parts):
  v,f=s.tessellate(.08);meshes.append(dict(model=path,part=i,material=mat,vertices=[[p.x,p.y,p.z] for p in v],faces=f))
for s in specs:
 x0,x1,y0,y1=s['bounds'];w=x1-x0;d=y1-y0;h=s['height'];kind=s['kind'];parts=[]
 if kind=='module':
  if s['ref']=='U6':x0,x1,y0,y1=-10,10,-6.19,13.81;w=d=20;s['source']='DFRobot nominal 20x20 mm PCB; footprint pad datum; component height/headers estimated'
  parts=[(box(x0,y0,2,w,d,1.2),'pcb')]
  if s['ref']=='U6':parts.append((box(x0+1,y0+1,3.2,14,13,1.8),'metal'));h=5
  else:parts.append((box(x0+2,y0+2,3.2,w-4,d-4,h-3.2),'dark'))
 elif kind=='switch':
  parts=[(box(-3,-1.8,.1,6,3.6,6),'dark'),(box(-3,-1.85,.1,6,.2,6),'metal'),(Part.makeCylinder(1.15,2,V(0,-1.8,3.6),V(0,-1,0)),'blue')]
 elif kind=='capacitor':
  parts=[(Part.makeCylinder(4,12,V(0,1.6,4.2),V(0,1,0)),'dark')]
 elif kind=='usb':
  outer=box(x0,y0,.1,w,d,h);inner=box(x0+.3,y0-.1,.45,w-.6,d+.2,h-.7);parts=[(outer.cut(inner),'metal'),(box(x0+1,y0+.7,1.35,w-2,d-1.4,.55),'dark')]
 else:
  if kind=='inductor' and s['ref']=='L4':x0,x1,y0,y1=-4,4,-4.25,4.25;w,d=8,8.5
  if kind=='inductor' and s['ref']=='L2':x0,x1,y0,y1=-1,1,.5,2.1;w,d=2,1.6
  parts=[(box(x0,y0,.05,w,d,h),'ivory' if kind in ['connector','led'] else 'dark')]
  if kind=='led':parts.append((box(x0+.3,y0+.3,h,w-.6,d-.6,.05),'metal'))
 for x,y,dx,dy in s['pads']:
  if kind in ['module','switch','capacitor']:parts.append((Part.makeCylinder(.25,3,V(x,y,-1)),'metal'))
  else:parts.append((box(x-dx*.35,y-dy*.35,0,dx*.7,dy*.7,.12),'metal'))
 save(parts,s['path']);s['height']=h
# Photo scale near socket: approx 19.5 px/mm, total 14.3 mm; axial seating estimated.
socket=Part.makeCylinder(.6,3.2,V(0,0,-3.2)).fuse(Part.makeCylinder(.3,4.6,V(0,0,-7.8))).fuse(Part.makeCylinder(.85,1.5)).fuse(Part.makeCylinder(.75,5,V(0,0,1.5))).cut(Part.makeCylinder(.525,5.6,V(0,0,1)))
save([(socket,'gold')],'socket_photo_estimate.step')
boards=json.loads((P/'boards.json').read_text());fit={f['ref']:f for f in json.loads((P/'tube_fit.json').read_text())}
for f in boards['display_board_in4']['footprints']:
 if not f['ref'].startswith('N'):continue
 parts=[]
 for pad in f['pads']:
  if not pad['n']:continue
  sh=socket.copy();x,y=pad['local'];sh.translate(V(x,-y,0));parts.append((sh,'gold'))
 save(parts,'display_board_in4_'+f['ref']+'_sockets.step')
(P/'model_specs.json').write_text(json.dumps(specs,indent=2));(P/'generated_meshes.json').write_text(json.dumps(meshes));print('Created',len(specs),'component models, 6 socket arrays and one socket model')
