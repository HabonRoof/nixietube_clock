from pathlib import Path
import json,html
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import Paragraph
from reportlab.lib.colors import HexColor
R=Path(__file__).resolve().parents[3]; A=R/'doc/user_guide/assets'; O=R/'output/pdf/nixie_clock_user_guide_zh_TW.pdf'
pdfmetrics.registerFont(TTFont('TC','/System/Library/Fonts/Supplemental/Arial Unicode.ttf'))
W,H=595.28,841.89
c=canvas.Canvas(str(O),pagesize=(W,H));c.setTitle('輝光管時鐘使用指南 | Revision H | Android WebUI');c.setAuthor('Nixie Clock Project')
ink='#253238';accent='#AD592F'; muted='#607078';bg='#F8F6F1'; pages=[];checks=[]
def para(t,x,y,w,size=11,color=ink):
 p=Paragraph(html.escape(t).replace('\n','<br/>'),ParagraphStyle('p',fontName='TC',fontSize=size,leading=size*1.65,textColor=HexColor(color),wordWrap='CJK'))
 _,h=p.wrap(w,1000);p.drawOn(c,x,H-y-h);checks.append((len(pages),y+h,t[:25]));return y+h

def text(t,x,y,size=11,color=ink):
 c.setFillColor(HexColor(color));c.setFont('TC',size);c.drawString(x,H-y-size,t)
def rect(x,y,w,h,color):
 c.setFillColor(HexColor(color));c.rect(x,H-y-h,w,h,fill=1,stroke=0)
def img(name,x,y,w,h):c.drawImage(str(A/name),x,H-y-h,w,h,mask='auto')
def begin(title,sub=''):
 if pages:c.showPage()
 pages.append({'title':title,'sub':sub,'blocks':[]})
 rect(0,0,W,H,bg);text('NIXIE CLOCK  /  USER GUIDE',40,25,9,accent);text(f'{len(pages):02d}',530,25,10,accent)
 text(title,40,62,25);para(sub,40,106,515,10,muted)
 rect(40,140,515,1,'#D9D6CE');text('REVISION H  •  繁體中文  •  2026.09',40,805,8,muted);text(f'{len(pages):02d}',535,805,8,muted)
 c.bookmarkPage(str(len(pages)));c.addOutlineEntry(title,str(len(pages)),0)
def block(head,body,x=40,y=170,w=515):
 pages[-1]['blocks'].append((head,body));y=para(head,x,y,w,14,accent);return para(body,x,y+7,w,11)+19

def phone(name):
 rect(39,165,234,512,'#20272B');rect(44,170,224,34,'#E8EAED');text('16:30    Android / Chrome',53,178,9)
 img(name+'.png',44,204,224,446);rect(44,650,224,22,'#FFFFFF');rect(122,661,70,3,'#222222')
 para('Android 行動版模擬；WebUI 取自專案原始碼。\n網路、時間、曲目均為示範資料。',40,690,233,8,muted)
def ui(title,sub,name,blocks):
 begin(title,sub);phone(name);y=167
 for h,b in blocks:y=block(h,b,298,y,257)
 assert y<786,(title,y)

begin('輝光管時鐘','使用指南  /  User Guide')
img('hero.png',0,177,W,372)
text('把時間，調成你的日常。',40,584,22)
para('從第一次連線到日常操作，依照手機畫面完成校時、鬧鐘、休眠與燈光設定。',40,631,500,13)
para('適用：Revision H 六管外殼示意與本專案目前韌體。\n本指南以 Android 手機操作 WebUI；無需安裝專用 App。\n外觀為指定 Blender 模型渲染，非實物照片；材質與發光效果以實機為準。',40,700,515,10,muted)

begin('01  第一次使用','依序完成以下五件事，就能開始使用。')
y=block('1  放好時鐘並接上電源','將時鐘與托架放在穩固、乾燥的平面，確認外殼完整。使用適用於本機的 USB 供電與線材，接至背面 USB-C 孔。供電額定值以實機標示或組裝者提供的規格為準。')
y=block('2  開啟設定 Wi-Fi','同時長按 BTN1 與 BTN2 約 3 秒。記下輝光管顯示的四位數代碼，例如 3847。',y=y)
y=block('3  讓 Android 連上時鐘','手機連接 NixieClock-3847，密碼為 nixie2026。即使顯示「沒有網際網路」，仍選擇保持連線。用 Chrome 開啟 http://192.168.8.8/。',y=y)
y=block('4  同步時間與儲存設定','先確認手機日期、時間與時區正確，再按「自動同步時間」。依需要儲存家用 WiFi、鬧鐘、休眠時段與燈光設定檔。',y=y)
y=block('5  完成並關閉設定 WiFi','確認各項儲存成功，再按「完成並關閉設定 WiFi」。約 3 秒後設定網路關閉；手機若未自動恢復原網路，請手動切回。',y=y)
para('查閱：按鍵 p.3｜模式 p.4-6｜Android 連線 p.7｜時間 p.8｜家用 WiFi p.9\n鬧鐘 p.10｜休眠 p.11｜燈光 p.12-13｜音訊 p.14｜疑難排解 p.15',40,720,515,10,muted)

begin('02  認識外觀與按鍵','下圖為從背面看向時鐘。按鍵編號從 BTN0 開始，請依圖辨識。')
img('rear.png',40,157,515,322)
points=json.loads((A/'rear_points.json').read_text())
for n,label,tx,ty in [('BTN2_button','BTN2',90,200),('BTN1_button','BTN1',195,181),('BTN0_button','BTN0',360,200),('USB_measured_reference','USB-C',370,453)]:
 px,py=points[n];px=40+px*515;py=157+py*322
 c.setStrokeColor(HexColor('#F4B65C'));c.setLineWidth(1.2);c.line(tx+20,H-ty-17,px,H-py);rect(tx-5,ty-2,65,22,'#253238');text(label,tx,ty,11,'#FFFFFF')
y=block('BTN0  長方形按鍵','鬧鐘響起時停止鬧鐘；番茄鐘待開始時啟動倒數；世界線變動率動畫中重新觸發動畫。',y=498)
y=block('BTN1 / BTN2  兩顆圓形按鍵','BTN1 短按切換顯示模式；BTN2 短按循環切換四組燈光設定檔。番茄鐘模式中，BTN2 不切換設定檔。',y=y)
y=block('BTN1 ＋ BTN2  同時長按 3 秒','開啟或關閉設定 Wi-Fi。待機變暗後，第一次短按僅恢復亮度；再按一次才執行一般按鍵功能。',y=y)

mode_cards=[
('mode_clock','時鐘  /  HHMMSS','163045  =  16:30:45','時鐘模式下約 1 分鐘沒有按鍵操作，管亮度與背光降為目前設定檔的 25%。第一次短按恢復亮度。'),
('mode_date','日期  /  YYMMDD','260910  =  2026 年 09 月 10 日','顯示目前日期。要回到時鐘可繼續短按 BTN1 循環切換。'),
('mode_pomodoro','番茄鐘  /  25 分鐘工作、5 分鐘休息','002500  =  25 分鐘，等待開始','進入時背光為靜態紅色；按 BTN0 開始倒數，工作紅色呼吸、休息綠色呼吸，兩段自動交替。BTN0 不提供暫停；按 BTN1 離開後恢復燈光設定檔。'),
('mode_divergence','世界線變動率  /  隨機數字動畫','482691  =  動畫中的範例瞬間','這是娛樂性數字效果，每次數字可能不同。動畫結束後自動回到時鐘；播放中可按 BTN0 再次觸發。'),
('mode_cathode','防陰極中毒  /  數字輪巡','555555  =  輪巡至數字 5 的瞬間','所有管子同步輪巡數字，結束後自動回到時鐘。起始數字隨機，不固定從 0 或 5 開始。')]
for spread,indices in enumerate([(0,1),(2,3),(4,)]):
 begin('03  五種顯示模式（'+str(spread+1)+'/3）','短按 BTN1：時鐘 → 日期 → 番茄鐘 → 世界線變動率 → 防陰極中毒 → 時鐘。')
 y=157
 for idx in indices:
  name,heading,caption,body=mode_cards[idx]
  text(heading,40,y,15,accent)
  pages[-1]['blocks'].append((heading,''))
  img(name+'.png',97.5,y+27,400,168.89)
  y=block(caption,body,y=y+200)
  pages[-1].setdefault('mode_images',[]).append(name)
  y+=5
 if spread==2:
  y=block('為什麼管子會短暫跳號？','韌體每 15 分鐘安排自動輪巡；休眠或部分互動狀態下會延後執行。這是維護顯示管的正常行為，不代表時間被重設。',y=y+12)
  y=block('看圖辨識：數字 5 → 6 → 7 → …','圖中只呈現輪巡的一個瞬間。下一步六管會一起換成另一個數字，完成後自動回到時鐘。',y=y)
 para('3D 發光示意：直接點亮 Revision H 模型的數字陰極，字元為亮橘色。背光與亮度以實機為準。',40,765,515,9,muted)

begin('04  Android 連線設定','Android 系統設定示意；不同手機品牌的選單名稱可能略有差異。')
rect(40,164,233,450,'#20272B');rect(45,170,223,437,'#FFFFFF')
text('16:30',59,183,10);text('設定  /  網際網路',59,219,18)
text('Wi-Fi 已開啟',59,266,12);rect(59,301,195,77,'#E8F0FE');text('NixieClock-3847',69,313,14);text('已連線，無網際網路',69,345,10)
text('MyHomeWiFi',59,405,13);text('已儲存',59,432,10,muted)
text('Chrome 網址列',59,482,11);rect(57,512,199,40,'#F0F2F4');text('http://192.168.8.8/',64,523,12)
para('上圖為重繪的 Android 系統流程示意，\n不是實際手機截圖。3847 為範例代碼。',40,636,234,9,muted)
y=167
for h,b in [('1  啟動設定模式','同時長按 BTN1＋BTN2 約 3 秒，依輝光管當次代碼選擇網路。每次開啟，代碼可能不同。'),('2  加入設定網路','開啟手機「設定 → 網路和網際網路 → 網際網路／Wi-Fi」，選擇 NixieClock-XXXX，輸入 nixie2026。'),('3  保持連線','若 Android 詢問是否使用沒有網際網路的網路，選擇保持連線。這個網路只用來設定時鐘。'),('4  開啟 WebUI','在 Chrome 網址列輸入完整網址 http://192.168.8.8/，不要輸入到搜尋欄，也不要改成 https。')]:y=block(h,b,298,y,257)
para('設定網路最長開啟 15 分鐘；所有裝置離線後約 2 分鐘自動關閉。逾時請重新長按按鍵、重新選擇當次網路。',40,714,515,11)

ui('05  時間：校時與狀態','底部「時間」頁籤；向下滑動還有 WiFi、完成設定、鬧鐘與休眠。','time',[
('1  先確認手機時間','在 Android 設定確認日期、時間與時區正確。台灣使用 UTC+8。'),
('2  自動同步時間','點藍色按鈕，會將目前手機的日期、時間及時區寫入時鐘。這個操作不需要時鐘連上網際網路。'),
('3  確認結果','看到「時間同步成功」及綠色「時鐘已同步」，再核對時間讀數。初次開啟未校準的時鐘時，頁面也會嘗試自動使用手機時間。'),
('看懂狀態燈','綠色：已同步且時間有效。黃色：正在檢查、尚未同步或備用電源狀態需確認。紅色：時間無效或連線／寫入出錯。這是 WebUI 的時鐘狀態，並非外殼 Wi-Fi 指示燈。')])

ui('06  家用 WiFi：每天自動校時','「時間」頁向下滑至「家用 WiFi（NTP 校時）」。','wifi',[
('1  輸入家中網路','手動輸入 WiFi 名稱（SSID）與密碼，名稱與大小寫需完全相符。使用提供 2.4 GHz 連線的家用網路。'),
('2  按「儲存 WiFi」','看到「WiFi 已儲存」後，確認「目前已設定」的名稱。這代表資料已存入時鐘，不代表已成功連線或完成 NTP 校時。'),
('3  日常校時方式','每日本地時間約 06:00，時鐘短暫連線取得網路時間並校準 RTC，其餘時間不維持家用 WiFi 連線。需要立即校時請用「自動同步時間」。'),
('換網路或清除','更換路由器時重新輸入名稱與完整密碼再儲存。密碼不會回填，勿把空白當成保留密碼。「清除 WiFi 設定」會停止後續家用網路校時，直到重新設定。')])

ui('07  設定每日鬧鐘','「時間」頁向下滑至「鬧鐘設定」。','alarm',[
('1  啟用與設定時間','將「啟用鬧鐘」選為開啟。時、分、秒採 24 小時制，例如 07:30:00。鬧鐘使用時鐘目前的本地時間，每日重複。'),
('2  選擇鬧鐘曲目','從「鬧鐘曲目」選單選擇已載入的曲目。可先到「音訊」頁試聽，並調整適當音量。'),
('3  儲存鬧鐘','按「儲存鬧鐘」，確認成功提示。響起時按 BTN0 停止；要停用之後的每日鬧鐘，將啟用選為關閉並再次儲存。'),
('沒有可用曲目？','代表音訊曲目尚未成功載入。請先處理音訊／SD 卡問題，再重新載入頁面。本版提供一組每日鬧鐘，沒有星期排程或貪睡設定。')])

ui('08  休眠與待機','「時間」頁最下方的「休眠時段」。','sleep',[
('設定休眠時段','開啟休眠模式，輸入開始與結束時間，例如 23:00 至 07:00，可跨午夜。開始與結束相同代表沒有有效休眠時段。'),
('確認已儲存','按「儲存休眠時段」並確認成功提示。若設定涵蓋現在時間，時鐘可能隨即熄滅；這不表示當機。'),
('夜間看一下時間','休眠時管子與背光皆關閉。短按任一按鍵，顯示時間約 5 秒，管亮度固定為 50（0-255），背光仍關閉。預覽期間再按會延長顯示。'),
('待機是另一種行為','一般時鐘模式閒置約 1 分鐘，亮度降低至設定檔的 25%，不會全黑。第一次按鍵喚醒亮度；要切模式或設定檔，請再按一次。')])

ui('09  燈光：調出喜歡的外觀','點底部「燈光」，可即時預覽背光與輝光管亮度。','light',[
('顏色與 RGB','點選色塊挑選顏色，或拖動 R、G、B 滑桿微調。每色範圍為 0-255，分別代表紅、綠、藍。'),
('背光亮度','範圍 0-255；0 為關閉，255 為最高設定值。這個滑桿調整管後方的 LED，管內數字亮度需到下方另調。'),
('四種動畫','靜態：固定顏色。呼吸：亮暗循環。彩虹：顏色循環變化。關閉：熄滅背光。彩虹模式下看到的顏色會持續變化。'),
('預覽之後要儲存','拖曳只提供暫時預覽。停止更新約 2 秒後，實機可能恢復原設定檔，但表單值仍保留。要保留新外觀，繼續向下滑並按「儲存 1-4」。')])

ui('10  輝光管與四組設定檔','「燈光」頁往下滑，可調整數字顯示並儲存整組外觀。','profiles',[
('輝光管亮度與切換','亮度範圍 0-255。開啟「數字淡入淡出」讓數字漸變；關閉則直接切換。這些值與背光一起存入設定檔。'),
('儲存 1、2、3、4','按其中一個儲存按鈕，把目前外觀覆寫至該格，同時套用該設定檔。看到「設定檔 N 已儲存」才算完成。'),
('色塊是套用，不是儲存','點左側小色塊，會套用該格原先存好的設定。時鐘上短按 BTN2 也能循環切換四組。'),
('離開前的確認','離開「燈光」頁會取消未儲存預覽。建議先儲存，再切到「時間」按「完成並關閉設定 WiFi」。可將四格分別設為日間、夜間、彩虹與低亮度。')])

ui('11  音訊：音量與試聽','點底部「音訊」。圖中三首曲目僅為模擬資料。','audio',[
('用 − / ＋ 調整音量','音量範圍為 0-30，0 為靜音。按鍵調整會直接送出並儲存，沒有另外的儲存音量按鈕。可重新載入頁面確認數值。'),
('點選曲目播放','點 mp3/0001.mp3 等曲目開始播放。再次點同一首可暫停，再點可繼續；點另一首切換曲目。依畫面「播放中／已暫停」辨識狀態。'),
('與鬧鐘搭配使用','先試聽並調整音量，再回「時間」頁選擇鬧鐘曲目並儲存。音量為共用設定，請勿將音量設為 0 後期待鬧鐘仍有聲音。'),
('曲目清單為空','請由組裝者確認 SD 卡已安裝，mp3 資料夾含有 0001.mp3 等正確檔案。WebUI 提供播放控制，沒有上傳音樂功能。')])

begin('12  遇到問題時','先確認設定網路仍在，再依症狀處理。')
y=170
for h,b in [('找不到 NixieClock-XXXX','重新同時長按 BTN1＋BTN2 約 3 秒，對照管上新的代碼。舊的網路名稱可能不再使用。'),('連上 Wi-Fi，網頁卻打不開','確認手機保持連在時鐘網路，使用 http://192.168.8.8/。若手機自動改用其他 Wi-Fi，重新選回；VPN 或自動網路切換影響連線時，暫時停用後再試。'),('儲存後，頁面失去連線','按「完成並關閉設定 WiFi」後斷線是正常的。逾時也會關閉設定網路。若不確定是否儲存成功，重新連線並核對設定值。'),('時間不準或顯示尚未同步','先核對手機時間與時區，再按「自動同步時間」。綠色「時鐘已同步」不是每日 NTP 成功紀錄；本版 WebUI 未提供 NTP 最近成功時間的顯示。'),('黑屏、變暗或燈光恢復原狀','黑屏先按任一鍵測試是否為休眠；變暗可能是待機。調燈光後恢復原狀，請確認已按「儲存 1-4」，不是只拖動滑桿。'),('鬧鐘或試聽沒有聲音','確認音量大於 0、曲目可以載入、鬧鐘已啟用且已儲存。若仍失敗，交由組裝者檢查音訊模組與 SD 卡。')]:y=block(h,b,y=y)

begin('13  維護與版本說明','保留本指南，日後更換手機或家用 Wi-Fi 時可再次依步驟設定。')
y=block('日常照顧','搬動時握住外殼與底座，避免撞擊管面。清潔前關閉本機電源並移除外接供電，使用乾燥軟布；不要讓液體進入外殼。機內含高壓電路與電池，內部維修、管子或 SD 卡處理請交由熟悉本機的組裝者進行。')
y=block('適用範圍','本指南依 2026-09-10 工作目錄的 README、WebUI 與模式控制程式製作。Revision H 外觀採六管模型，保留原模型外殼、按鍵與 15° 托架。模式章節以亮橘色發光材質模擬數字陰極；圖片為靜態示意，不代表實物亮度、材質或量產認證。',y=y)
y=block('畫面如何製作','WebUI 圖片直接擷取 src/web_page.cpp 內嵌 HTML，以 Android 行動瀏覽器尺寸 412 × 820 模擬並注入示範資料；非 Android 實機或時鐘連線驗收。系統 Wi-Fi 頁為流程重繪示意。',y=y)
y=block('內容依據','README.md\nsrc/web_page.cpp、src/system_controller.cpp\nsrc/daemons/display_daemon.cpp、doc/web_api.md\nhardware/case/revision_h/clock_case_revision_h.blend\nhardware/case/revision_h/README.md',y=y)
y=block('編排參考','Omnixie Clock v1.1.0 User Manual（EN），2024-08。參考 Android 逐步連線與設定頁圖說的組織方式，本文內容與圖片依本專案重新製作。',y=y)
para('參考原文：Omnixie Clock User Manual',40,y,515,10,accent)
c.linkURL('https://nixieclock.org/wp-content/uploads/2024/08/Omnixie-Clock-v1.1.0-User-Manual-EN.pdf',(40,H-y-20,400,H-y),relative=0)
c.save()
md=['# 輝光管時鐘使用指南','', '繁體中文 · Revision H · Android WebUI · 2026-09-10','']
for p in pages:
 md+=['## '+p['title'],p['sub'],'']
 for h,b in p['blocks']:md+=['### '+h,b,'']
 for name in p.get('mode_images',[]):md+=['![模式 3D 模擬圖](assets/'+name+'.png)','']
(R/'doc/user_guide/USER_GUIDE.zh-TW.md').write_text('\n'.join(md))
(R/'tmp/pdfs/layout_qa.json').write_text(json.dumps({'pages':len(pages),'overflows':[x for x in checks if x[1]>793]},ensure_ascii=False,indent=2))
print('Created',O,'pages',len(pages));print('Overflows',[x for x in checks if x[1]>793])
