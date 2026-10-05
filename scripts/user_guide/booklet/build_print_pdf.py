"""Build A5 reading PDF from the validated PPT's 300 dpi page renders."""
from pathlib import Path
import sys
from reportlab.pdfgen import canvas
from reportlab.lib.utils import ImageReader
from pypdf import PdfReader,PdfWriter
from pypdf.generic import NameObject,DictionaryObject
root=Path(__file__).resolve().parents[3]
images=root/'tmp/booklet/print-render'
temp=root/'tmp/booklet/booklet-flat.pdf';out=root/'output/pdf/nixie_clock_booklet_zh_TW.pdf'
w,h=148*72/25.4,210*72/25.4
c=canvas.Canvas(str(temp),pagesize=(w,h),pageCompression=1)
c.setTitle('輝光管時鐘使用說明書｜A5 右裝訂');c.setAuthor('Nixie Clock Project')
for n in range(1,33):
 p=images/f'slide-{n}.png';assert p.exists(),p
 c.drawImage(ImageReader(str(p)),0,0,w,h);c.showPage()
c.save()
r=PdfReader(temp);writer=PdfWriter();writer.append(r)
writer._root_object.update({NameObject('/ViewerPreferences'):DictionaryObject({NameObject('/Direction'):NameObject('/R2L')}),NameObject('/PageLayout'):NameObject('/TwoPageLeft')})
writer.add_metadata({'/Title':'輝光管時鐘使用說明書｜A5 右裝訂','/Author':'Nixie Clock Project'})
with out.open('wb') as f:writer.write(f)
print(out)
