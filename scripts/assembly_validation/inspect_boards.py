import re,json,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def parse(s):
 tokens=iter(re.findall(r'"(?:\\.|[^"\\])*"|[^\s()]+|[()]',s))
 def read():
  a=[]
  for t in tokens:
   if t==')':return a
   a.append(read() if t=='(' else t[1:-1] if t.startswith('"') else t)
  return a
 return read()[0]
def alln(n,k):return [v for v in n if isinstance(v,list) and v and v[0]==k]
def one(n,k,d=None):return next(iter(alln(n,k)),d)
def inspect(name):
 b=parse((ROOT/f'hardware/{name}/{name}.kicad_pcb').read_text());out=[]
 for f in alln(b,'footprint'):
  props={p[1]:p[2] for p in alln(f,'property')};a=list(map(float,one(f,'at')[1:]));a=a+[0]*(3-len(a));th=math.radians(a[2]);pads=[]
  for p in alln(f,'pad'):
   q=list(map(float,one(p,'at')[1:3]));x=a[0]+q[0]*math.cos(th)+q[1]*math.sin(th);y=a[1]-q[0]*math.sin(th)+q[1]*math.cos(th)
   pads.append(dict(n=p[1],xy=[x,y],local=q,size=one(p,'size')[1:],drill=one(p,'drill',[])[1:],net=one(p,'net',[])[1:]))
  out.append(dict(ref=props.get('Reference'),value=props.get('Value'),fp=f[1],at=a,layer=one(f,'layer')[1],models=alln(f,'model'),pads=pads))
 return dict(footprints=out,edges=[e for e in b if isinstance(e,list) and e[0].startswith('gr_') and one(e,'layer',['',''])[1]=='Edge.Cuts'],thickness=one(one(b,'general'),'thickness'))
if __name__=='__main__':
 data={n:inspect(n) for n in ['main_board','display_board_in4']};(Path(__file__).parent/'boards.json').write_text(json.dumps(data,indent=2))
 for n,b in data.items():
  print(n,'edges',b['edges'],'thickness',b['thickness'])
  for f in b['footprints']:
   if not f['models'] or f['ref'].startswith(('J','N','H')):print(f['ref'],f['value'],f['fp'],f['at'],f['layer'],'models:',[m[1] for m in f['models']])
