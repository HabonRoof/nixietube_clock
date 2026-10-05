import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
const runtimeModules=process.env.RUNTIME_NODE_MODULES||'/Users/johnson/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const runtimeRequire=createRequire(path.join(runtimeModules,'package.json'));
const sharp=runtimeRequire('sharp');
const {Presentation,PresentationFile}=await import(runtimeRequire.resolve('@oai/artifact-tool'));
const ROOT=process.cwd();
const AS=path.join(ROOT,'doc/user_guide/booklet/assets');
const SKILL='/Users/johnson/.codex/plugins/cache/openai-primary-runtime/presentations/26.905.11957/skills/presentations';
const TMP=path.join(ROOT,'tmp/booklet-v2');
const W=148/25.4*96,H=210/25.4*96,FONT='Arial Unicode MS';
const BG='#17110E',INK='#F4E8D5',ACC='#FFB265',MUT='#C4AF98';
const pres=Presentation.create({slideSize:{width:W,height:H}});
let slide,pg=0,left=45,CW=W-113;const contents=[],boxAudit=[];
function wrap(s,w,size){
 const max=w/size;let out=[];
 for(const para of s.split('\n')){let line='',width=0;for(const ch of (para.match(/[A-Za-z0-9_./:+-]+|./gu)||[])){const n=[...ch].reduce((v,c)=>v+(/[\u0000-\u00ff]/.test(c)?(/[ilI1 .,:]/.test(c)?.28:.56):1),0);if(width+n>max&&line&&!/^[，。；：！？、）」』]/u.test(ch)){out.push(line);line='';width=0;}line+=ch;width+=n;}out.push(line);}
 return out.join('\n');
}
function txt(s,x,y,w,size=16,color=INK,{height,bold=false,align='left',wrapped=true,fill='none'}={}){
 s=s.replace(/BTN0(?!_)/g,'開始／停止按鈕').replace(/BTN1(?!_)/g,'模式切換按鈕').replace(/BTN2(?!_)/g,'顏色循環按鈕');
 const val=wrapped?wrap(s,w,size):s;const lines=val.split('\n').length;const h=height??Math.max(size*1.6,lines*size*1.48+3);
 const box=slide.shapes.add({geometry:'textbox',name:'文字 '+s.slice(0,22),position:{left:x,top:y,width:w,height:h},fill,line:{fill:'none',width:0}});
 box.text=val;box.text.style={typeface:FONT,fontSize:size,color,bold,alignment:align,verticalAlignment:'top',lineSpacing:1.25,autoFit:'none',wrap:'none',insets:{left:0,right:0,top:0,bottom:0}};
 boxAudit.push({page:pg,text:s.slice(0,30),x,y,w,h,bottom:y+h});return y+h;
}
async function pic(file,x,y,w,h,{crop,fit='contain'}={}){
 let bytes=await fs.readFile(path.join(AS,file+'.png'));
 if(crop){const m=await sharp(bytes).metadata();const x=Math.round(m.width*(crop.left||0)),y=Math.round(m.height*(crop.top||0));bytes=await sharp(bytes).extract({left:x,top:y,width:m.width-x-Math.round(m.width*(crop.right||0)),height:m.height-y-Math.round(m.height*(crop.bottom||0))}).png().toBuffer();}
 const im=slide.images.add({blob:bytes,contentType:'image/png',name:file,alt:file,fit,position:{left:x,top:y,width:w,height:h},});
 const meta=await sharp(bytes).metadata();const scale=Math.min(w/meta.width,h/meta.height);return {x:x+(w-meta.width*scale)/2,y:y+(h-meta.height*scale)/2,w:meta.width*scale,h:meta.height*scale};
}
function notes(extra=''){
 slide.speakerNotes.textFrame.setText('內容來源：專案 README.md、src/web_page.cpp、src/system_controller.cpp、src/daemons/display_daemon.cpp。外觀來源：hardware/case/revision_h/clock_case_revision_h.blend。'+extra);
}
function page(title,section='',opts={}){
 slide=pres.slides.add();pg++;left=pg%2?45:68;slide.background.fill=BG;contents.push({page:pg,title});
 if(!opts.clean){txt(section,left,38,CW,11,ACC);txt(title,left,67,CW,29,INK,{bold:false});txt(String(pg).padStart(2,'0'),pg%2?45:W-73,H-41,28,11,MUT,{align:pg%2?'left':'right'});}
 notes();return slide;
}
function block(head,body,y=150,size=16,w=CW,x=left){y=txt(head,x,y,w,18,ACC)+6;y=txt(body,x,y,w,size)+18;return y;}
function callout(s,x,y){txt(s,x,y,26,13,ACC,{height:23,fill:BG,align:'center'});}
// 01 Plain typography cover.
page('輝光管時鐘','',{clean:true});
txt('輝光管\n時鐘',63,169,420,56,INK,{height:184});
txt('使用說明書',66,395,420,25,ACC);
txt('Revision H\n繁體中文',66,610,420,14,MUT);
notes('封面全為可編輯文字，統一 Arial Unicode MS。A5 直式，右側裝訂。');
// Opening spread, page 2 is physically on the right in the right-bound booklet.
page('咖啡廳情境（右頁）','',{clean:true});await pic('cafe_photo_composite',0,0,W,H,{crop:{left:.5,right:0,top:0,bottom:0},fit:'cover'});notes('情境圖使用 Revision H 正面俯視渲染圖合成寫實照片，依整機260×80.12×84 mm與杯子規格約束視覺比例，非精密尺寸圖。咖啡杯參考 KINTO SCS250ml的φ80×H90×W105mm，書本為A5 148×210mm。https://kinto-europe.com/products/27635 下午16:30:45。頁 2 為跨頁右半部，頁 3 為左半部。');
page('咖啡廳情境（左頁）','',{clean:true});await pic('cafe_photo_composite',0,0,W,H,{crop:{left:0,right:.5,top:0,bottom:0},fit:'cover'});notes('場景為生成式照片合成，非實物攝影。時鐘參考原模型渲染，頂部按鈕與桌面使用一致視角；前景已移除多餘雜物。');
// 04 Contents after spread.
page('目錄','閱讀指引');
let y=155;
for(const [label,num] of [['開始使用與按鍵','05'],['五種顯示模式','07'],['Chrome 連線設定','13'],['連線完成與外觀','14'],['WebUI 操作與儲存','15'],['疑難排解與維護','23'],['整機爆炸圖','25'],['零件圖與開發參考','27']]){txt(label,left,y,CW-50,20);txt(num,left+CW-40,y,40,20,ACC,{align:'right'});y+=57;}
txt('本冊以六管 Revision H 外觀與目前韌體說明。\n內文橫排，書脊位於右側。',left,671,CW,14,MUT);
// 05 Quickstart.
page('第一次使用','開始使用');
y=145;
y=block('1　接上供電','將時鐘與托架放在乾燥穩固的平面，使用適用本機的 USB 供電與線材，接至背面 USB-C。額定值以實機標示或組裝者提供的規格為準。',y);
y=block('2　開啟設定 Wi-Fi','同時長按 BTN1 與 BTN2 約 3 秒，記下管上四位數代碼，例如 3847。',y);
y=block('3　用手機連線','連接 NixieClock-3847，密碼為 nixie2026，再開啟 http://192.168.8.8/。統一使用 Chrome，步驟詳見第 13 頁。',y);
y=block('4　校時並完成設定','核對手機時間與時區，按「自動同步時間」。各項設定儲存成功後，在「時間」頁按「完成並關閉設定 WiFi」。',y);
// 06 Front-facing position guide with function names.
async function frontControls(y=215){
 await pic('line_top',left-10,y-30,CW+20,210,{crop:{left:.17,right:.17,top:.25,bottom:.28}});
 txt('開始／停止',left+180,160,110,14,ACC);
 txt('模式切換',left+266,183,90,14,ACC);
 txt('顏色循環',left+340,158,90,14,ACC);
}
page('按鈕位置與功能','開始使用');await frontControls();
y=block('開始／停止按鈕','啟動番茄鐘倒數、停止正在響的鬧鐘，或重新觸發世界線變動率動畫。',433,15);
y=block('模式切換按鈕','短按依序切換時鐘、日期、番茄鐘、世界線變動率與防陰極中毒模式。',y,15);
y=block('顏色循環按鈕','短按切換四組已儲存的燈光設定檔。番茄鐘模式中不切換設定檔。',y,15);
notes('正面略俯視，一般50mm相機。功能對應：開始／停止=BTN0，模式切換=BTN1，顏色循環=BTN2。標線端點來自原模型按鈕中心投影。');
// 07 Modes overview.
page('五種顯示模式','日常操作');
y=153;
for(const [n,title,body] of [['01','時鐘','顯示時、分、秒。'],['02','日期','顯示年、月、日。'],['03','番茄鐘','工作 25 分鐘，休息 5 分鐘。'],['04','世界線變動率','隨機數字動畫，結束後自動回到時鐘。'],['05','防陰極中毒','所有管子輪巡數字，完成後回到時鐘。']]){txt(n,left,y,45,25,ACC);txt(title,left+58,y,CW-58,21);txt(body,left+58,y+38,CW-58,15,MUT);y+=105;}
txt('短按 BTN1 依上列順序循環切換。',left,715,CW,15,MUT);
// 08-12: mode plates, original 3D geometry.
const modes=[
['時鐘','mode_clock','16:30:45','163045 代表下午 4 點 30 分 45 秒。','日常顯示','時鐘模式下，約 1 分鐘沒有按鍵操作，輝光管與背光亮度會降為目前設定檔的 25%。短按任一鍵可恢復亮度。'],
['日期','mode_date','2026 年 09 月 10 日','260910 依序表示兩位數年份、月份與日期。','切換方式','從時鐘模式短按一次 BTN1 進入日期。繼續短按 BTN1 可依序切換其他模式，最後回到時鐘。'],
['番茄鐘','mode_pomodoro','00:25:00','002500 是進入番茄鐘後等待開始的畫面。','開始、休息與離開','按 BTN0 開始 25 分鐘倒數。工作期間背光紅色呼吸，休息 5 分鐘時改為綠色呼吸，兩段自動交替。倒數中 BTN0 不提供暫停，按 BTN1 可離開並恢復燈光設定檔。'],
['世界線變動率','mode_divergence','482691','圖中數字是動畫的一個瞬間，每次可能不同。','動畫操作','進入後會播放隨機數字效果，結束後自動回到時鐘。動畫播放中按 BTN0 可再次觸發。這是娛樂性顯示，數值不代表量測結果。'],
['防陰極中毒','mode_cathode','555555','圖中為六管同時輪巡至數字 5 的瞬間。','自動輪巡','起始數字隨機，之後六管一起切換數字，結束後自動回到時鐘。韌體另有每 15 分鐘自動輪巡機制，休眠或部分互動狀態下會延後執行。']];
for(const [title,file,digits,cap,h,b] of modes){page(title,'顯示模式');const box=await pic('line_horizontal',left-10,166,CW+20,221,{crop:{left:.19,right:.19,top:.34,bottom:.30}}); const num=file==='mode_clock'?'163045':file==='mode_date'?'260910':file==='mode_pomodoro'?'002500':file==='mode_divergence'?'482691':'555555'; for(let i=0;i<6;i++)txt(num[i],box.x+box.w*(.127+i*.129),box.y+box.h*.235,box.w*.10,31,'#FFFFFF',{align:'center',wrapped:false});txt(digits,left,426,CW,31,ACC);txt(cap,left,479,CW,16);let q=block(h,b,544,16);if(title==='番茄鐘')txt('等待開始時背光為靜態紅色。此模式中 BTN2 不切換設定檔。',left,q,CW,14,MUT);else txt('六位數字為線稿示意。',left,715,CW,12,MUT);}
// 13 &14 mobile connection: actual browser screenshots plus full steps.
function steptext(items,x,y,w,fsz=14){for(const [h,b] of items){y=txt(h,x,y,w,16,ACC)+4;y=txt(b,x,y,w,fsz)+15;}return y;}
page('Chrome 連線設定','手機設定');await pic('android_time',left,170,181,360);txt('Chrome 設定頁示意',left,539,190,11,MUT);
steptext([['1　加入時鐘網路','長按 BTN1＋BTN2 約 3 秒。手機開啟「設定」中的 Wi-Fi，選擇管上代碼對應的 NixieClock-XXXX。'],['2　輸入密碼','輸入 nixie2026。若手機提示沒有網際網路，仍選擇保持連線。'],['3　開啟 Chrome','在網址列輸入完整的 http://192.168.8.8/，即可看到設定頁。']],left+207,167,CW-207,14);
txt('設定網路最多開啟 15 分鐘。所有裝置離線約 2 分鐘後也會自動關閉。逾時請重新長按按鍵，依新的代碼再次連線。',left,630,CW,15);
notes('畫面由 Chromium 行動版尺寸模擬。API 使用示範資料。Android 選單名稱因品牌而異。');
page('連線完成與外觀','手機設定');
await pic('line_back',left,144,CW,180,{crop:{left:.18,right:.18,top:.33,bottom:.32}});
txt('背面 USB-C 供電接孔',left,338,CW,14,ACC);
y=block('完成後關閉設定 Wi-Fi','在 Chrome 的「時間」頁按「完成並關閉設定 WiFi」。若手機未自動恢復原網路，請在 Wi-Fi 設定手動切回。',387,16);
await pic('line_left',left,535,CW,168,{crop:{left:.19,right:.18,top:.27,bottom:.19}});
notes('連線流程統一使用 Chrome。外觀採使用者提供線稿。');
// 15 Common navigation.
page('WebUI 的三個頁籤','設定介面');
await pic('android_time',left,142,CW,124,{crop:{top:.90,bottom:0}});
y=block('時間','同步時間、設定家用 WiFi、鬧鐘與休眠，並在完成後關閉設定 WiFi。',301,16);
y=block('燈光','調整背光、輝光管亮度與數字淡入淡出，儲存並套用四組設定檔。',y,16);
y=block('音訊','調整共用音量，點選曲目試聽、暫停或繼續播放。',y,16);
y=block('各區分別儲存','WiFi、鬧鐘與休眠各有儲存按鈕。燈光按「儲存 1」至「儲存 4」，音量調整會直接儲存。',y,16);
// 16-22 detailed cards for print readability.
async function detail(title,file,caption,blocks,{imgHeight=345,imgWidth=310}={}){page(title,'WebUI 操作');await pic(file,left+(CW-imgWidth)/2,143,imgWidth,imgHeight);txt(caption,left,143+imgHeight+9,CW,11,MUT);let by=143+imgHeight+42;for(const [h,b] of blocks)by=block(h,b,by,14.5);}
await detail('同步時間','android_detail_clock','Chrome　時鐘狀態',[['從手機設定時鐘','先確認手機的日期、時間與時區正確，再按「自動同步時間」。此操作會把手機時間及時區寫入時鐘。台灣使用 UTC+8。'],['確認同步成功','看到「時間同步成功」及綠色「時鐘已同步」後，再核對讀數。黃色代表尚待確認，紅色代表時間或連線出錯。']],{imgHeight:302,imgWidth:330});
await detail('家用 WiFi 校時','ios_detail_wifi','Chrome　家用 WiFi',[['輸入並儲存網路','填入提供 2.4 GHz 連線的家用 WiFi 名稱與完整密碼，按「儲存 WiFi」。看到「WiFi 已儲存」後，再核對目前設定的名稱。'],['每天 06:00 自動校時','時鐘每日短暫連線校時，其餘時間不維持家用 WiFi。儲存成功只表示資料已寫入，需立即校時時請按「自動同步時間」。']],{imgHeight:332,imgWidth:300});
await detail('每日鬧鐘','android_detail_alarm','Chrome　鬧鐘設定',[['設定時間與曲目','將啟用鬧鐘選為開啟，以 24 小時制選擇時、分、秒，再選擇曲目並按「儲存鬧鐘」。可先到音訊頁試聽及調整音量。'],['停止或停用鬧鐘','響起時按 BTN0 停止。若要停用之後的每日鬧鐘，選擇關閉並再次儲存。本版提供一組每日鬧鐘。']],{imgHeight:319,imgWidth:310});
await detail('休眠時段','ios_detail_sleep','Chrome　休眠時段',[['設定夜間休眠','開啟休眠模式，設定開始及結束時間，例如 23:00 至 07:00，再按「儲存休眠時段」。可跨午夜，兩個時間相同則無有效休眠時段。'],['夜間查看時間','休眠時管子與背光會熄滅。短按任一鍵，時間暫時顯示約 5 秒，管亮度固定為 50，背光仍關閉。']],{imgHeight:312,imgWidth:308});
await detail('背光與數字外觀','android_detail_light','Chrome　背光調整',[['調整並觀察外觀','點選顏色或拖曳 R、G、B 滑桿，各色與亮度範圍均為 0-255。動畫可選靜態、呼吸、彩虹或關閉。'],['把預覽存成設定檔','下方還有輝光管亮度與數字淡入淡出。停止拖曳約 2 秒，實機可能恢復原設定。要保留調整，請依下一頁步驟儲存。']],{imgHeight:348,imgWidth:290});
await detail('儲存與套用設定檔','ios_detail_profiles','Chrome　四組示範設定檔',[['將目前外觀儲存到指定位置','先調整背光顏色、亮度、動畫與輝光管外觀，再向下滑至燈光設定檔。選好要覆寫的位置，例如第 2 組，按右側「儲存 2」。看到「設定檔 2 已儲存」後，時鐘會套用這組新設定。'],['日後重新使用已儲存的外觀','點選左側第 2 組色塊，即可套用剛才儲存的設定。也可短按時鐘 BTN2 循環切換四組。儲存完成後，再切到時間頁關閉設定 WiFi。']],{imgHeight:278,imgWidth:302});
page('音量與曲目','WebUI 操作');await pic('android_audio',left+65,138,CW-130,366);txt('音訊頁：音量與曲目',left,494,CW,11,MUT);
y=block('音量調整','按減號或加號調整 0-30 的共用音量，0 為靜音，調整會直接送出並儲存。鬧鐘也使用此音量。',531,14.5);
y=block('曲目試聽','點一首曲目開始播放，再點同一首可暫停或繼續，點另一首則切換曲目。若清單為空，請由組裝者檢查 SD 卡與 mp3 資料夾。',y,14.5);
// 23 Troubleshooting.
page('疑難排解','日常維護');
y=143;
for(const [h,b] of [['找不到設定 Wi-Fi','重新長按 BTN1＋BTN2 約 3 秒，依管上新的四位數代碼選擇網路。'],['網頁無法開啟','確認手機仍連接時鐘 Wi-Fi，輸入完整的 http://192.168.8.8/。若逾時斷線，重新啟動設定模式。'],['時間不準','先核對手機時間與時區，再按「自動同步時間」。綠色同步狀態不等於每日 NTP 最近成功紀錄。'],['黑屏或突然變暗','按任一鍵確認是否為休眠或待機。調燈光後恢復原狀時，確認已按指定設定檔的儲存按鈕。'],['鬧鐘或試聽沒有聲音','確認音量大於 0、曲目可載入，且鬧鐘已啟用並儲存。仍有問題時交由組裝者檢查。']])y=block(h,b,y,15);
// 24 Care.
page('使用與清潔','日常維護');
y=151;
y=block('放置與搬動','放在乾燥、平穩且不易碰撞的位置。搬動時握住外殼與底座，避免撞擊管面或拉扯供電線。',y);
y=block('清潔前處理供電','關閉本機電源並移除外接供電後，使用乾燥軟布清潔。避免液體進入外殼。',y);
y=block('需要打開外殼時','機內含有高壓電路與電池。內部維修、輝光管或 SD 卡處理，請交由熟悉本機的組裝者進行。',y);
y=block('設定資料的保存','一般設定會保存在時鐘本機。更換手機後重新加入設定 Wi-Fi 即可管理。更換路由器時，重新輸入家用 WiFi 名稱與完整密碼後儲存。',y);
// 25 Exploded main plate.
page('整機爆炸圖','開發參考');
const explodedItems=[['part_front_shell','01 前殼',145],['tubes','02 輝光管與管座',215],['display_board','03 顯示板',285],['spacers','04 墊柱',355],['main_board','05 主板',425],['part_rear_cover','06 背殼',495],['screws','07 固定螺絲',565],['part_BTN0_button','08 操作按鈕',635]];
for(const [file,label,y] of explodedItems){if(file==='part_front_shell'){await pic(file,left+190,y,CW-210,65);await pic('part_tilt_base_15deg',left+100,y,90,65);}else if(file==='part_BTN0_button'){await pic(file,left+100,y,110,65);await pic('part_BTN1_button',left+220,y,80,65);await pic('part_BTN2_button',left+305,y,80,65);}else await pic(file,left+100,y,CW-120,65);txt(label,left,y+20,130,13,ACC);}
txt('各層分離顯示，間距為辨識示意。',left,718,CW,13,MUT);
notes('每一層嵌入可旋轉 GLB 模型，使用原始 Revision H 零件。');
// 26 Key.
page('爆炸圖中的組件','開發參考');
y=148;
for(const [n,h,b] of [['01','前殼與托架','前殼固定六管外觀，15° 托架支撐時鐘。'],['02','六支 IN-4 與管座','每管 14 個 socket，共 84 個。管腳方向需依實物確認。'],['03','顯示板','配置六管插座、顯示驅動與背光元件。'],['04','四組 PCB 墊柱','選用無牙通孔絕緣墊柱，模型僅保留參考高度。'],['05','主板','整合控制、RTC、音訊、電源與電池管理功能。'],['06','背殼','與前殼包覆雙板組件，保留供電接孔。'],['07','固定螺絲','依原模型的螺絲與嵌件配置組裝，長度需核對。'],['08','操作按鈕','開始／停止、模式切換及顏色循環，位置見第 6 頁。']]){txt(n,left,y,35,17,ACC);txt(h,left+47,y,CW-47,17);txt(b,left+47,y+29,CW-47,13.8,MUT);y+=70;}
// 27-28 printable parts.
async function partrow(file,title,body,y){await pic(file,left,y,230,141);txt(title,left+249,y+12,CW-249,19,ACC);txt(body,left+249,y+51,CW-249,14);}
page('外殼與托架零件','開發參考');
await partrow('part_front_shell','前殼 × 1','front_shell.stl\n六個顯示開孔與內部支撐結構。',151);
await partrow('part_rear_cover','背殼 × 1','rear_cover.stl\n保留供電接孔與固定結構。',332);
await partrow('part_tilt_base_15deg','托架 × 1','tilt_base_15deg.stl\n15° 擺放角度。',513);
txt('各零件可在 PowerPoint 選取並調整 3D 角度。',left,710,CW,13,MUT);
page('按鍵操作速查','日常操作');await frontControls();
y=block('開啟或關閉設定 Wi-Fi','同時長按「模式切換按鈕」與「顏色循環按鈕」約 3 秒。',433,15);
y=block('番茄鐘與鬧鐘','番茄鐘等待時，按「開始／停止按鈕」啟動倒數；倒數中不提供暫停。鬧鐘響起時，按同一按鈕停止響鈴。',y,15);
y=block('變暗或休眠時查看時間','待機變暗後，第一次短按只恢復亮度。休眠期間短按任一按鈕，可查看時間約 5 秒。',y,15);
notes('按鈕零件形狀改以使用者可辨識的正面位置及功能說明。工程代號只保留於備忘稿：BTN0開始／停止，BTN1模式切換，BTN2顏色循環。');
// 29 Boards.
page('電子板與輝光管','開發參考');
await pic('main_board',left,139,CW,162);txt('主板',left,307,CW,18,ACC);txt('ESP32-S3 控制、DS3231 RTC、音訊與電源管理。',left,336,CW,14);
await pic('display_board',left,397,CW,155);txt('六管顯示板',left,561,CW,18,ACC);txt('PCA9685、陽極多工與背光電路，連接六支 IN-4。',left,592,CW,14);
txt('板上部分元件採簡化封裝或暫定高度。圖面供辨識架構，實際接線與料件仍需對照 KiCad。',left,674,CW,14,MUT);
// 30 Mechanical constraints with tube and hardware.
page('管座、墊柱與配合','開發參考');await pic('in4_tube_socket',left,139,163,241);await pic('fasteners',left+188,157,CW-188,158);
txt('IN-4 與 socket',left,390,170,14,ACC);txt('四組固定與墊柱',left+188,331,CW-188,14,ACC);
y=block('墊柱先量測再選用','建議無牙通孔絕緣墊柱，內孔 3.2-3.3 mm、外徑不大於 6 mm。模型的板間距 6.635 mm 來自通用接頭模型，需在實際接頭自然插合後量測。',444,15);
y=block('仍待確認的機構條件','兩板接頭有 X 向 0.25 mm 偏移。管座尺寸、管腳字形基準與零件高度含建模假設，應先試裝與量測，勿用鎖緊螺絲強迫板件彎曲。',y,15);
notes('來源：hardware/case/revision_h/README.md、scripts/assembly_validation/README.md。數值為專案模型限制，非量產公差認證。');
// 31 developer paths and sources.
page('開發檔案與版本','開發參考');
await pic('github_qr',left+105,153,CW-210,CW-210);
txt('掃描 QR Code 開啟 GitHub 專案',left,412,CW,18,ACC,{align:'center'});
txt('HabonRoof / nixietube_clock',left,451,CW,17,INK,{align:'center'});
y=block('原始碼與模型','專案包含韌體、WebUI、Revision H 外殼模型、列印檔與裝配驗證資料。版本與更新內容請以 GitHub 專案為準。',512,16);
y=block('本冊版本','2026 年 09 月修訂。適用 Revision H 六管模型。手機截圖沿用前版示範畫面。',y,15);
notes('QR Code: https://github.com/HabonRoof/nixietube_clock');
// 32 Text back cover.
page('封底','',{clean:true});txt('輝光管時鐘',63,243,420,33);txt('Revision H',65,316,420,18,ACC);txt('使用指南與開發參考\n繁體中文',65,592,420,15,MUT);
if(pg!==32)throw new Error('Expected booklet page count multiple of four: '+pg);
await fs.mkdir(path.join(TMP,'previews'),{recursive:true});
await (await PresentationFile.exportPptx(pres)).save(path.join(TMP,'candidate.pptx'));
await fs.writeFile(path.join(TMP,'contents.json'),JSON.stringify(contents,null,2));await fs.writeFile(path.join(TMP,'box_audit.json'),JSON.stringify(boxAudit,null,2));
for(let i=0;i<pres.slides.items.length;i++){
 const s=pres.slides.items[i];const png=await pres.export({slide:s,format:'png',scale:1.5});await fs.writeFile(path.join(TMP,'previews',`page-${String(i+1).padStart(2,'0')}.png`),new Uint8Array(await png.arrayBuffer()));
 const l=await s.export({format:'layout'});await fs.writeFile(path.join(TMP,'previews',`page-${String(i+1).padStart(2,'0')}.json`),await l.text());
}
console.log('BUILT',pg,'PAGES');
