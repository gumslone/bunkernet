#pragma once
#include <pgmspace.h>

static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>BunkerNet</title>
<style>
:root{--bg:#0f1419;--p:#18202a;--p2:#222c38;--t:#e6edf3;--m:#8b98a5;--a:#3ea6ff;--g:#3fb950;--r:#f85149}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--t);font:15px system-ui,sans-serif;height:100vh;height:100dvh;display:flex;flex-direction:column}
header{display:flex;align-items:center;gap:8px;padding:8px 12px;background:var(--p);border-bottom:1px solid var(--p2)}
header b{flex:1}#st{font-size:12px;color:var(--m)}#st.bad{color:var(--r)}
nav{display:flex;background:var(--p)}nav button{flex:1;padding:9px;background:none;border:0;border-bottom:2px solid transparent;color:var(--m);font:inherit}
nav button.on{color:var(--t);border-color:var(--a)}
main{flex:1;overflow:auto;padding:10px;display:none}main.on{display:block}
#chat.on{display:flex;flex-direction:column;padding:0}
#convs{display:flex;gap:6px;overflow-x:auto;padding:8px;background:var(--p)}
.chip{white-space:nowrap;padding:5px 11px;border-radius:14px;background:var(--p2);color:var(--t);border:0;font:inherit;font-size:13px}
.chip.off{opacity:.55}.chip.on{background:var(--a);color:#001}.chip i{font-style:normal;background:var(--r);color:#fff;border-radius:8px;padding:0 5px;margin-left:5px;font-size:11px}
#msgs{flex:1;overflow:auto;padding:10px}
.m{max-width:82%;margin:4px 0;padding:6px 10px;border-radius:10px;background:var(--p2);word-wrap:break-word}
.m.me{margin-left:auto;background:#17436b}.m small{display:block;color:var(--m);font-size:11px}
.m s{text-decoration:none;color:var(--m);font-size:11px;margin-left:6px}.m s.ok{color:var(--g)}
form{display:flex;gap:6px;padding:8px;background:var(--p)}
input{flex:1;min-width:0;padding:10px;border-radius:8px;border:1px solid var(--p2);background:var(--bg);color:var(--t);font:inherit}
button.go{padding:10px 16px;border:0;border-radius:8px;background:var(--a);color:#001;font:inherit;font-weight:600}
table{width:100%;border-collapse:collapse}td,th{padding:7px 4px;border-bottom:1px solid var(--p2);text-align:left}th{color:var(--m);font-weight:500;font-size:12px}
.d{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--g);margin-right:6px}
#nm{position:fixed;inset:0;background:var(--bg);display:flex;flex-direction:column;gap:10px;align-items:center;justify-content:center;padding:20px}
#nm[hidden]{display:none}
#rf{display:flex;gap:6px;padding:6px 8px;background:var(--p)}#rf[hidden]{display:none}
#cw{background:#9a6700;color:#fff;font-size:12px;padding:5px 12px}#cw:empty{display:none}
.m.pr{border-left:3px solid var(--r)}#pb{padding:10px;border:0;border-radius:8px;background:var(--p2);font:inherit}#pb.on{background:var(--r)}
#lg{flex:none;padding:8px;border-radius:8px;border:1px solid var(--p2);background:var(--bg);color:var(--t);font:inherit}
.lo{color:var(--r)}
#cnt{color:var(--m);font-size:11px;align-self:center}
#who{background:var(--p2);color:var(--t);border:0;border-radius:12px;padding:3px 10px;font:inherit;font-size:12px}
.day{text-align:center;color:var(--m);font-size:11px;margin:10px 0 4px}.m s.t{margin-left:8px}
#al{background:var(--r);color:#fff}.al{display:flex;align-items:center;gap:8px;padding:8px 12px;border-bottom:1px solid #0004;font-weight:600}.al span{flex:1}.al button{border:0;border-radius:6px;padding:4px 8px;font:inherit;font-size:12px}
.go.red{background:var(--r);color:#fff}#ab{padding:10px;border:0;border-radius:8px;background:var(--p2);font:inherit}
</style></head><body>
<header><b>BunkerNet</b><span id="st" data-i="connecting"></span><button id="who" data-ti="changeName"></button></header>
<div id="al"></div><div id="cw"></div>
<nav><button data-t="chat" class="on" data-i="chat"></button><button data-t="data" data-i="data"></button><button data-t="nodes" data-i="nodes"></button></nav>
<main id="chat" class="on"><div id="convs"></div>
<form id="rf" hidden><input id="rn" data-p="roomPh"><button class="go" data-i="join"></button></form>
<div id="msgs"></div>
<form id="f"><button type="button" id="ab" data-ti="alertTitle">🚨</button><button type="button" id="pb" data-ti="prioTitle">❗</button><input id="x" autocomplete="off"><small id="cnt"></small><button class="go" id="sb"></button></form></main>
<main id="data"><table><thead><tr><th data-i="key"></th><th data-i="value"></th><th data-i="from"></th><th data-i="updated"></th></tr></thead><tbody id="dt"></tbody></table>
<form id="df"><input id="dk" data-p="keyPh" style="flex:.6"><input id="dv" data-p="valPh"><button class="go" data-i="set"></button></form></main>
<main id="nodes"><p id="ni" style="color:var(--m)"></p><table><thead><tr><th data-i="node"></th><th data-i="hops"></th><th data-i="seen"></th><th data-i="supply"></th></tr></thead><tbody id="nt"></tbody></table></main>
<div id="nm" hidden><h2 data-i="welcome"></h2><input id="ni2" data-p="namePh" style="flex:none;width:240px"><select id="lg"><option value="en">English</option><option value="de">Deutsch</option><option value="uk">Українська</option></select><button class="go" id="nb" data-i="join"></button><button class="chip" id="nc" hidden data-i="cancel"></button><p data-i="hintPopup" style="color:var(--m);font-size:12px;max-width:290px;text-align:center"></p></div>
<script>
const L={
en:{connecting:'connecting…',reconnecting:'reconnecting…',nodeOnline:'node {0} · {1} online',chat:'Chat',data:'Data',nodes:'Nodes',everyone:'Everyone',msgPh:'Message…',offlinePh:'{0} is offline: will be stored',alertPh:'Emergency alert text…',send:'Send',alertBtn:'ALERT',stored:'⏳ stored',ok:'OK',clearAll:'Clear all',key:'Key',value:'Value',from:'From',updated:'Updated',noData:'No data yet',keyPh:'key',valPh:'value',set:'Set',node:'Node',hops:'Hops',seen:'Seen',supply:'Supply',welcome:'Welcome to BunkerNet',namePh:'Your name',join:'Join',cancel:'Cancel',hintPopup:'If this pop-up keeps forgetting your name, open http://192.168.4.1 in your normal browser.',nodeInfo:'This node: {0} · up {1}s · free heap {2} · clients {3} · mesh time {4}',defkey:' · ⚠ default mesh key in use, anyone can read the mesh',clockWarn:'Your device clock is {0} min off from the mesh time. Your messages are stamped with the mesh time.',changeName:'Change your name',alertTitle:'Emergency alert',prioTitle:'Priority message',room:'+ Room',roomPh:'Room name',leave:'Leave room',lowV:'low supply voltage'},
de:{connecting:'Verbinde…',reconnecting:'Verbindung wird wiederhergestellt…',nodeOnline:'Knoten {0} · {1} online',chat:'Chat',data:'Daten',nodes:'Knoten',everyone:'Alle',msgPh:'Nachricht…',offlinePh:'{0} ist offline: wird gespeichert',alertPh:'Text der Notfallmeldung…',send:'Senden',alertBtn:'ALARM',stored:'⏳ gespeichert',ok:'OK',clearAll:'Für alle löschen',key:'Schlüssel',value:'Wert',from:'Von',updated:'Aktualisiert',noData:'Noch keine Daten',keyPh:'Schlüssel',valPh:'Wert',set:'Setzen',node:'Knoten',hops:'Hops',seen:'Zuletzt gesehen',supply:'Spannung',welcome:'Willkommen bei BunkerNet',namePh:'Ihr Name',join:'Beitreten',cancel:'Abbrechen',hintPopup:'Wenn dieses Pop-up Ihren Namen immer wieder vergisst, öffnen Sie http://192.168.4.1 in Ihrem normalen Browser.',nodeInfo:'Dieser Knoten: {0} · Laufzeit {1} s · freier Speicher {2} · Clients {3} · Mesh-Zeit {4}',defkey:' · ⚠ Standard-Mesh-Schlüssel aktiv, jeder kann das Netz mitlesen',clockWarn:'Die Uhr Ihres Geräts weicht {0} Min. von der Mesh-Zeit ab. Ihre Nachrichten tragen die Mesh-Zeit.',changeName:'Namen ändern',alertTitle:'Notfallalarm',prioTitle:'Wichtige Nachricht',room:'+ Raum',roomPh:'Raumname',leave:'Raum verlassen',lowV:'niedrige Spannung'},
uk:{connecting:'з\'єднання…',reconnecting:'перепідключення…',nodeOnline:'вузол {0} · {1} онлайн',chat:'Чат',data:'Дані',nodes:'Вузли',everyone:'Усі',msgPh:'Повідомлення…',offlinePh:'{0} офлайн: повідомлення буде збережено',alertPh:'Текст тривоги…',send:'Надіслати',alertBtn:'ТРИВОГА',stored:'⏳ збережено',ok:'OK',clearAll:'Скасувати для всіх',key:'Ключ',value:'Значення',from:'Від',updated:'Оновлено',noData:'Даних ще немає',keyPh:'ключ',valPh:'значення',set:'Записати',node:'Вузол',hops:'Кроки',seen:'Бачили',supply:'Живлення',welcome:'Ласкаво просимо до BunkerNet',namePh:'Ваше ім\'я',join:'Приєднатися',cancel:'Скасувати',hintPopup:'Якщо це вікно постійно забуває ваше ім\'я, відкрийте http://192.168.4.1 у звичайному браузері.',nodeInfo:'Цей вузол: {0} · працює {1} с · вільна пам\'ять {2} · клієнтів {3} · час мережі {4}',defkey:' · ⚠ використовується стандартний ключ мережі, будь-хто може читати трафік',clockWarn:'Годинник вашого пристрою відрізняється від часу мережі на {0} хв. Ваші повідомлення позначаються часом мережі.',changeName:'Змінити ім\'я',alertTitle:'Сигнал тривоги',prioTitle:'Важливе повідомлення',room:'+ Кімната',roomPh:'Назва кімнати',leave:'Покинути кімнату',lowV:'низька напруга живлення'}};
const $=s=>document.querySelector(s),E=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const ls=(k,v)=>{try{if(v===undefined)return localStorage.getItem(k);localStorage.setItem(k,v)}catch(e){return null}};
let lang=ls('lang')||(navigator.language||'en').slice(0,2);if(!L[lang])lang='en';
const t=(k,...a)=>(L[lang][k]||L.en[k]||k).replace(/\{(\d)\}/g,(m,i)=>a[i]);
const rh=n=>{const a=new Uint8Array(n);try{crypto.getRandomValues(a)}catch(e){for(let i=0;i<n;i++)a[i]=Math.random()*256}return Array.from(a,b=>('0'+b.toString(16)).slice(-2)).join('')};
let me=ls('uid'),tok=ls('tok');
function newIdentity(){do{me=rh(4)}while(me=='00000000');tok=rh(16);ls('uid',me);ls('tok',tok)}
if(!me||!tok)newIdentity();
const bl=s=>{try{return new TextEncoder().encode(s).length}catch(e){return s.length}};
function clamp(el,max){el.addEventListener('input',()=>{let v=el.value;if(bl(v)>max){const a=Array.from(v);while(bl(a.join(''))>max)a.pop();el.value=a.join('')}if(el.id=='x')cnt()})}
function cnt(){const n=bl($('#x').value);$('#cnt').textContent=n>110?n+'/176':''}
const roomHash=n=>{let h=0x811c9dc5;for(const c of unescape(encodeURIComponent(n.trim().toLowerCase()))){h^=c.charCodeAt(0);h=Math.imul(h,0x01000193)}return('0000000'+(h>>>0).toString(16)).slice(-8)};
let lastAct=Date.now(),loaded=0;
let name=ls('name')||'',since=0,dv=-1,av=-1,alerts=[],seenAl={},seenP={},hidAl={},alertMode=0,prioMode=0,msgs=[],acked=new Set(),users=[],unread={},cur='all',busy=0,tab='chat',node='',seenM=new Set(),contacts={},rooms={};
try{contacts=JSON.parse(ls('contacts')||'{}')}catch(e){}
try{rooms=JSON.parse(ls('rooms')||'{}')}catch(e){}
const online=id=>users.some(u=>u.id==id);
const uname=id=>{const u=users.find(u=>u.id==id);return u?u.n:(contacts[id]||id.slice(0,4))};
const inRoom=()=>cur.slice(0,2)=='r:';
const ph=()=>alertMode?t('alertPh'):(cur!='all'&&!inRoom()&&!online(cur)?t('offlinePh',uname(cur)):t('msgPh'));
function formState(){$('#sb').className='go'+(alertMode?' red':'');$('#sb').textContent=alertMode?t('alertBtn'):t('send');$('#pb').className=prioMode?'on':'';$('#x').placeholder=ph()}
function applyLang(){document.documentElement.lang=lang;
 document.querySelectorAll('[data-i]').forEach(e=>e.textContent=t(e.dataset.i));
 document.querySelectorAll('[data-p]').forEach(e=>e.placeholder=t(e.dataset.p));
 document.querySelectorAll('[data-ti]').forEach(e=>e.title=t(e.dataset.ti));
 $('#lg').value=lang;who();formState()}
function askName(edit){if(name&&!edit)return;$('#ni2').value=name;$('#nc').hidden=!name;$('#nm').hidden=0;$('#ni2').focus()}
function who(){$('#who').textContent='👤 '+(name||'?')}
$('#who').onclick=()=>askName(1);$('#nc').onclick=()=>{$('#nm').hidden=1};
$('#lg').onchange=()=>{lang=$('#lg').value;ls('lang',lang);applyLang();render();if(tab=='nodes')nodes()};
$('#nb').onclick=()=>{const v=$('#ni2').value.trim();if(!v)return;name=v;ls('name',v);$('#nm').hidden=1;who();poll()};
const p2=n=>('0'+n).slice(-2),tm=ms=>{const d=new Date(ms);return p2(d.getHours())+':'+p2(d.getMinutes())},
dayk=ms=>{const d=new Date(ms);return d.getFullYear()+'-'+d.getMonth()+'-'+d.getDate()},
dlab=ms=>new Date(ms).toLocaleDateString(lang,{weekday:'short',day:'numeric',month:'short'}),
stamp=ms=>dayk(ms)==dayk(Date.now())?tm(ms):dlab(ms)+' '+tm(ms);
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{tab=b.dataset.t;document.querySelectorAll('nav button,main').forEach(e=>e.classList.remove('on'));b.classList.add('on');$('#'+tab).classList.add('on');if(tab=='nodes')nodes()});
const key=m=>m.t==1?'all':m.t==8?'r:'+m.to:(m.f==me?m.to:m.f);
const chip=(c,label,cls,n)=>'<button class="chip'+(cls||'')+(cur==c?' on':'')+'" data-c="'+c+'">'+E(label)+(n?'<i>'+n+'</i>':'')+'</button>';
function render(){
 let h=chip('all',t('everyone'),'',unread.all);
 Object.keys(rooms).forEach(r=>h+=chip('r:'+r,'# '+rooms[r],'',unread['r:'+r]));
 const peers={};Object.keys(contacts).forEach(id=>peers[id]={id,n:contacts[id],off:1});users.forEach(u=>peers[u.id]={id:u.id,n:u.n,off:0});delete peers[me];
 Object.values(peers).forEach(u=>h+=chip(u.id,u.n,u.off?' off':'',unread[u.id]));
 h+='<button class="chip off" data-c="+">'+E(t('room'))+'</button>';
 if(inRoom())h+='<button class="chip off" data-c="-">'+E(t('leave'))+'</button>';
 $('#convs').innerHTML=h;
 $('#convs').querySelectorAll('.chip').forEach(c=>c.onclick=()=>{const v=c.dataset.c;
  if(v=='+'){$('#rf').hidden=!$('#rf').hidden;if(!$('#rf').hidden)$('#rn').focus();return}
  if(v=='-'){delete rooms[cur.slice(2)];ls('rooms',JSON.stringify(rooms));cur='all';render();return}
  cur=v;unread[cur]=0;render()});
 const box=$('#msgs'),stick=box.scrollTop+box.clientHeight>=box.scrollHeight-40;
 let ld='';formState();
 box.innerHTML=msgs.filter(m=>key(m)==cur).sort((a,b)=>a.ms-b.ms).map(m=>{const mine=m.f==me,dk=dayk(m.ms);let h='';if(dk!=ld){ld=dk;h='<div class="day">'+dlab(m.ms)+'</div>'}
  return h+'<div class="m'+(mine?' me':'')+(m.p?' pr':'')+'">'+(mine?'':'<small>'+E(m.n)+' · '+tm(m.ms)+'</small>')+(m.p?'❗ ':'')+E(m.x)+(mine?'<s class="t">'+tm(m.ms)+'</s>'+(m.t==2?'<s class="'+(acked.has(m.m)?'ok">✓✓':'">'+(online(m.to)?'✓':t('stored')))+'</s>':''):'')+'</div>'}).join('');
 if(stick)box.scrollTop=box.scrollHeight}
$('#rf').onsubmit=e=>{e.preventDefault();const n=$('#rn').value.trim();if(!n)return;const r=roomHash(n);rooms[r]=n;ls('rooms',JSON.stringify(rooms));$('#rn').value='';$('#rf').hidden=1;cur='r:'+r;since=0;render();poll()};
async function poll(){if(busy||!name)return;busy=1;
 try{const r=await fetch('/poll?since='+since+'&me='+me+'&tok='+tok+'&ts='+Math.floor(Date.now()/1000)+'&name='+encodeURIComponent(name)+'&dv='+dv+'&av='+av,{cache:'no-store'});
  if(r.status==403){newIdentity();since=0;seenM.clear();msgs=[];busy=0;return}   // id claimed by someone else: take a fresh identity
  const j=await r.json();
  if(node&&j.node!=node){node=j.node;since=0;busy=0;return}   // roamed to another node: its log numbering differs, resync
  node=j.node;users=j.users;since=j.last;
  users.forEach(u=>{if(u.id!=me&&contacts[u.id]!=u.n){contacts[u.id]=u.n;ls('contacts',JSON.stringify(contacts))}});
  let pn=0;
  j.msgs.forEach(m=>{if(m.t==5){acked.add(m.m);return}if(m.t==8&&!rooms[m.to])return;if(seenM.has(m.m))return;seenM.add(m.m);lastAct=Date.now();m.ms=m.s?m.s*1000:Date.now();msgs.push(m);const k=key(m);if(k!=cur&&m.f!=me)unread[k]=(unread[k]||0)+1;if(m.p&&m.f!=me&&loaded&&!seenP[m.m]){seenP[m.m]=1;pn=1}});
  if(pn){try{navigator.vibrate([250,100,250])}catch(e){}beep()}
  loaded=1;
  if(msgs.length>300)msgs=msgs.slice(-300);
  if(j.data){dv=j.dv;$('#dt').innerHTML=j.data.map(d=>'<tr><td>'+E(d.k)+'</td><td>'+E(d.v)+'</td><td>'+E(uname(d.f))+'</td><td>'+(d.s?stamp(d.s*1000):'–')+'</td></tr>').join('')||'<tr><td colspan=4 style="color:var(--m)">'+t('noData')+'</td></tr>'}
  if(j.alerts){av=j.av;alerts=j.alerts;let nw=0;alerts.forEach(a=>{if(!seenAl[a.m]){seenAl[a.m]=1;nw=1}});if(nw){try{navigator.vibrate([400,150,400,150,400])}catch(e){}beep()}drawAl()}
  const off=j.mt?Math.round(Math.abs(j.mt-Date.now()/1000)/60):0;$('#cw').textContent=off>=2?'🕐 '+t('clockWarn',off):'';
  $('#st').className='';$('#st').textContent=t('nodeOnline',node,users.length);render()
 }catch(e){$('#st').className='bad';$('#st').textContent=t('reconnecting')}busy=0}
function beep(){try{const c=new(window.AudioContext||window.webkitAudioContext)(),o=c.createOscillator();o.type='square';o.frequency.value=880;o.connect(c.destination);o.start();o.stop(c.currentTime+.6)}catch(e){}}
function drawAl(){$('#al').innerHTML=alerts.filter(a=>!hidAl[a.m]).map(a=>'<div class="al"><span>🚨 '+E(a.n)+': '+E(a.x)+'</span><button data-h="'+a.m+'">'+t('ok')+'</button><button data-c="'+a.m+'">'+t('clearAll')+'</button></div>').join('');
 $('#al').querySelectorAll('button').forEach(b=>b.onclick=()=>{if(b.dataset.h){hidAl[b.dataset.h]=1;drawAl()}else{post('/alertclear',{me,name,m:b.dataset.c})}})}
$('#ab').onclick=()=>{alertMode=!alertMode;formState()};
$('#pb').onclick=()=>{prioMode=!prioMode;formState()};
async function post(u,o){o.tok=tok;lastAct=Date.now();try{await fetch(u,{method:'POST',body:new URLSearchParams(o)})}catch(e){}poll()}
$('#f').onsubmit=e=>{e.preventDefault();const x=$('#x').value.trim();if(!x)return;$('#x').value='';cnt();
 if(alertMode){alertMode=0;formState();post('/alert',{me,name,x});return}
 const o={me,name,ts:Math.floor(Date.now()/1000),x,p:prioMode?'1':'0'};
 if(inRoom())o.room=cur.slice(2);else o.to=cur=='all'?'0':cur;
 prioMode=0;formState();post('/send',o)};
$('#df').onsubmit=e=>{e.preventDefault();const k=$('#dk').value.trim(),v=$('#dv').value.trim();if(!k)return;$('#dk').value=$('#dv').value='';post('/put',{me,name,ts:Math.floor(Date.now()/1000),k,v})};
async function nodes(){try{const j=await(await fetch('/nodes',{cache:'no-store'})).json();
 $('#ni').textContent=t('nodeInfo',j.id,j.up,j.heap,j.clients,j.mt?tm(j.mt*1000):'?')+(j.defkey?t('defkey'):'');$('#ni').style.color=j.defkey?'var(--r)':'';
 const vf=v=>v?'<span'+(v<3000?' class="lo" title="'+t('lowV')+'"':'')+'>'+(v/1000).toFixed(2)+' V</span>':'–';
 $('#nt').innerHTML=j.nodes.map(n=>'<tr><td><span class="d"></span>'+E(n.n)+' <small style="color:var(--m)">'+n.id+'</small></td><td>'+n.h+'</td><td>'+n.a+'s</td><td>'+vf(n.v)+'</td></tr>').join('')}catch(e){}}
clamp($('#x'),176);clamp($('#dk'),23);clamp($('#dv'),47);clamp($('#ni2'),23);clamp($('#rn'),23);
['click','keydown','touchstart'].forEach(e=>addEventListener(e,()=>{lastAct=Date.now()},{passive:true}));
document.addEventListener('visibilitychange',()=>{if(!document.hidden)poll()});
/* poll gently: 1.5 s while active, slower when idle or the tab is hidden */
function sched(){setTimeout(async()=>{await poll();if(tab=='nodes'&&!document.hidden)nodes();sched()},document.hidden?8000:(Date.now()-lastAct>30000?3500:1500))}
applyLang();sched();askName();poll();
</script></body></html>)HTML";
