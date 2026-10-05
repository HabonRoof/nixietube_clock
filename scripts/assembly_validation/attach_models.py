from pathlib import Path
import re,json
P=Path(__file__).parent;ROOT=P.parents[1]
specs=json.loads((P/'model_specs.json').read_text());changes=[]
for board in ['main_board','display_board_in4']:
 file=ROOT/f'hardware/{board}/{board}.kicad_pcb';text=file.read_text();lookup={s['ref']:s for s in specs if s['board']==board}
 def update(m):
  block=m.group();ref=re.search(r'\(property "Reference" "([^"]+)"',block).group(1)
  if ref not in lookup and not (board=='display_board_in4' and re.fullmatch('N[1-6]',ref)):return block
  paths=[lookup[ref]['path']] if ref in lookup else [f'{board}_{ref}_sockets.step',f'{board}_{ref}_tube.step']
  block=re.sub(r'\n\t\t\(model .*?\n\t\t\)', '',block,flags=re.S)
  model=''.join('\n\t\t(model "${KIPRJMOD}/../assembly_validation/models/'+p+'"\n\t\t\t(offset (xyz 0 0 0))\n\t\t\t(scale (xyz 1 1 1))\n\t\t\t(rotate (xyz 0 0 0))\n\t\t)' for p in paths)
  changes.append([board,ref,paths]);return block[:-3]+model+'\n\t)'
 new=re.sub(r'\n\t\(footprint .*?\n\t\)',update,text,flags=re.S)
 # Only 3D model annotations are modified; footprints, nets, tracks and pads stay unchanged.
 backup=P/(board+'_before_models.kicad_pcb')
 if not backup.exists():backup.write_text(text)
 file.write_text(new)
(P/'model_attachment_changes.json').write_text(json.dumps(changes,indent=2));print(len(changes),'footprints updated')
