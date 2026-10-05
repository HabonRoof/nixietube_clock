"""Impose 32 reading-order A5 pages onto A4 landscape, right binding."""
from pathlib import Path
import sys,json
from pypdf import PdfReader,PdfWriter,PageObject,Transformation
source=Path(sys.argv[1]);dest=Path(sys.argv[2]);r=PdfReader(source)
n=len(r.pages);assert n%4==0
mm=72/25.4;W,H=297*mm,210*mm
writer=PdfWriter();order=[]
for i in range(n//4):
 for a,b in [(1+2*i,n-2*i),(n-1-2*i,2+2*i)]:
  page=PageObject.create_blank_page(width=W,height=H)
  for slot,number in enumerate([a,b]):
   src=r.pages[number-1];sw,sh=float(src.mediabox.width),float(src.mediabox.height)
   scale=min((W/2)/sw,H/sh);dx=slot*W/2+(W/2-sw*scale)/2;dy=(H-sh*scale)/2
   page.merge_transformed_page(src,Transformation().scale(scale).translate(dx,dy))
  writer.add_page(page);order.append([a,b])
writer.add_metadata({'/Title':'輝光管時鐘使用說明書｜A4 雙面右裝訂拼版','/Author':'Nixie Clock Project'})
with dest.open('wb') as f:writer.write(f)
dest.with_suffix('.page-order.json').write_text(json.dumps(order,indent=2))
print(f'{n} A5 pages → {len(order)} A4 sides / {n//4} sheets')
