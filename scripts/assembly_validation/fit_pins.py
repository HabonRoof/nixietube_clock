import json,math
from pathlib import Path
P=Path(__file__).parent;b=json.loads((P/'boards.json').read_text());pins=json.loads((P.parent/'in4_tube_model/parameters.json').read_text())['pins']
labels=['4','6','8','A3','9','7','N/C','0','2','A2','3','5','A1','1'];results=[]
for f in sorted(b['display_board_in4']['footprints'],key=lambda f:f['ref']):
 if not f['ref'].startswith('N'):continue
 q={p['n']:p['xy'] for p in f['pads']};A=[(p['x'],p['y']) for p in pins];B=[q[k] for k in labels];ac=[sum(p[i] for p in A)/14 for i in (0,1)];bc=[sum(p[i] for p in B)/14 for i in (0,1)]
 aa=[(p[0]-ac[0],p[1]-ac[1]) for p in A];bb=[(p[0]-bc[0],p[1]-bc[1]) for p in B]
 angle=math.atan2(sum(a[0]*v[1]-a[1]*v[0] for a,v in zip(aa,bb)),sum(a[0]*v[0]+a[1]*v[1] for a,v in zip(aa,bb)));c=math.cos(angle);s=math.sin(angle);center=[bc[0]-c*ac[0]+s*ac[1],bc[1]-s*ac[0]-c*ac[1]]
 errors=[math.hypot(center[0]+c*a[0]-s*a[1]-v[0],center[1]+s*a[0]+c*a[1]-v[1]) for a,v in zip(A,B)]
 r=dict(ref=f['ref'],angle_clockwise_deg=math.degrees(angle),center=center,max_pin_error_mm=max(errors),rms_pin_error_mm=(sum(e*e for e in errors)/14)**.5,errors=dict(zip(labels,errors)))
 results.append(r);print(r)
(P/'tube_fit.json').write_text(json.dumps(results,indent=2))
