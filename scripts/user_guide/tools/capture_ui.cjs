const fs=require('fs'),path=require('path');
const {chromium}=require('/Users/johnson/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
(async()=>{
const raw=fs.readFileSync('src/web_page.cpp','utf8');
const html=[...raw.matchAll(/^\s*("(?:[^"\\]|\\.)*")/gm)].map(m=>JSON.parse(m[1])).join('');
fs.writeFileSync('doc/user_guide/assets/webui_source.html',html);
const browser=await chromium.launch({headless:true,executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'});
const ctx=await browser.newContext({viewport:{width:412,height:820},deviceScaleFactor:2,isMobile:true,hasTouch:true,userAgent:'Mozilla/5.0 (Linux; Android 14; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Mobile Safari/537.36'});
const page=await ctx.newPage();let errors=[];page.on('pageerror',e=>errors.push(e.message));
const display={backlight:{color:{r:255,g:128,b:0},brightness:128,effect:'static'},nixie:{brightness:180,transition:'fade'}};
const profileColors=[{r:255,g:128,b:0},{r:33,g:150,b:243},{r:76,g:175,b:80},{r:156,g:39,b:176}];
const settings={clock:{timezone_offset_hours:8,rtc_calibrated:true},alarm:{enabled:true,time:'07:30:00',track:1},display,audio:{volume:15},profiles:{active_index:0,items:profileColors.map((color,index)=>({index,display:{...display,backlight:{...display.backlight,color}}}))},hibernation:{enabled:true,start:'23:00',end:'07:00'}};
const data={'/api/settings':settings,'/api/time':{unix_utc:1789029000,time_valid:true,osf:false,clock:settings.clock},'/api/ap/status':{active:true,ssid:'NixieClock-3847',password:'nixie2026',session_code:3847,remaining_sec:842},'/api/wifi':{configured:true,ssid:'MyHomeWiFi'},'/api/audio/tracks':{folder:'mp3',count:3,tracks:[1,2,3].map(id=>({id,name:`mp3/000${id}.mp3`}))},'/api/audio/status':{track:1,state:'paused'}};
await page.route('http://192.168.8.8/**',r=>{const p=new URL(r.request().url()).pathname;r.fulfill({contentType:p==='/'?'text/html':'application/json',body:p==='/'?html:JSON.stringify(data[p]||{ok:true})});});
await page.goto('http://192.168.8.8/');await page.waitForFunction(()=>document.getElementById('wifi_ssid').value==='MyHomeWiFi');
async function shot(name,tab,selector){await page.evaluate(t=>showPage(t),tab);await page.evaluate(s=>{window.scrollTo(0,0);if(s){const el=document.querySelector(s);window.scrollTo(0,el.getBoundingClientRect().top+window.scrollY-12)}},selector);await page.screenshot({path:`doc/user_guide/assets/${name}.png`});}
await shot('time','time',null);
await shot('wifi','time','.page[data-page="time"] .card:nth-child(2)');
await shot('alarm','time','.page[data-page="time"] .card:nth-child(4)');
await shot('sleep','time','.page[data-page="time"] .card:nth-child(5)');
await shot('light','backlight',null);
await shot('profiles','backlight','.page[data-page="backlight"] .card:nth-child(2)');
await shot('audio','audio',null);
fs.writeFileSync('tmp/pdfs/ui_qa.json',JSON.stringify({errors,viewport:{width:412,height:820},source:'src/web_page.cpp',mocked:true},null,2));
await browser.close();})();
