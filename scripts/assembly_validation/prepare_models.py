from inspect_boards import *
import hashlib
P=Path(__file__).parent; specs=[]
for name in ['main_board','display_board_in4']:
 text=(ROOT/f'hardware/{name}/{name}.kicad_pcb').read_text();b=parse(text)
 for f in alln(b,'footprint'):
  props={p[1]:p[2] for p in alln(f,'property')};ref=props['Reference'];fp=f[1];models=alln(f,'model')
  if ref.startswith(('H','NT','TP','G')) or fp.startswith('nixies-us:'):continue
  if models and all(Path(m[1].replace('${KICAD9_3DMODEL_DIR}','/Applications/KiCad/KiCad.app/Contents/SharedSupport/3dmodels')).exists() for m in models):continue
  layer=one(f,'layer')[1];side=1 if layer=='B.Cu' else -1;pts=[]
  for e in f:
   if not isinstance(e,list) or not e[0].startswith('fp_') or one(e,'layer',['',''])[1] not in ('F.Fab','B.Fab'):continue
   if e[0] in ['fp_line','fp_rect']:
    pts += [list(map(float,one(e,k)[1:3])) for k in ['start','end']]
   if e[0]=='fp_poly':pts += [list(map(float,k[1:3])) for k in one(e,'pts')[1:]]
  source='footprint fabrication outline; height estimated';height=.9
  if not pts:
   for e in f:
    if isinstance(e,list) and e[0] in ['fp_line','fp_rect'] and one(e,'layer',['',''])[1] in ('F.SilkS','B.SilkS','F.CrtYd','B.CrtYd'):
     pts += [list(map(float,one(e,k)[1:3])) for k in ['start','end']]
   source='footprint silkscreen/courtyard envelope; height estimated'
  if not pts:raise ValueError((ref,fp))
  bounds=[min(p[0] for p in pts),max(p[0] for p in pts),min(side*p[1] for p in pts),max(side*p[1] for p in pts)]
  if 'PTS645' in fp:height=7;kind='switch'
  elif 'DFPlayer' in fp:height=4;kind='module'
  elif 'NCH8200' in fp:height=12;kind='module'
  elif 'Horizontal' in fp and 'Cap_' in fp:height=8;kind='capacitor'
  elif 'PowerInductor' in fp:height=5;kind='inductor'
  elif 'Murata' in fp:height=1.2;kind='inductor'
  elif 'Connector_USB' in fp:height=3.2;kind='usb'
  elif 'JST' in fp:height=4.8;kind='connector'
  elif 'WS2812' in fp:height=.9;kind='led'
  elif 'LTR-303' in fp:height=.6;kind='sensor'
  else:kind='ic'
  pads=[]
  for pad in alln(f,'pad'):
   if not pad[1]:continue
   q=list(map(float,one(pad,'at')[1:3]));sz=list(map(float,one(pad,'size')[1:3]));pads.append([q[0],side*q[1],*sz])
  specs.append(dict(board=name,ref=ref,fp=fp,value=props['Value'],bounds=bounds,height=height,kind=kind,pads=pads,source=source,path=f'{name}_{ref}.step'))
(P/'model_specs.json').write_text(json.dumps(specs,indent=2));print([(s['ref'],s['bounds'],s['kind']) for s in specs if not s['ref'].startswith('D')])
