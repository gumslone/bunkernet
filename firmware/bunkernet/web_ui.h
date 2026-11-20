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
#cnt{color:var(--m);font-size:11px;align-self:center}
#who{background:var(--p2);color:var(--t);border:0;border-radius:12px;padding:3px 10px;font:inherit;font-size:12px}
.day{text-align:center;color:var(--m);font-size:11px;margin:10px 0 4px}.m s.t{margin-left:8px}
#al{background:var(--r);color:#fff}.al{display:flex;align-items:center;gap:8px;padding:8px 12px;border-bottom:1px solid #0004;font-weight:600}.al span{flex:1}.al button{border:0;border-radius:6px;padding:4px 8px;font:inherit;font-size:12px}
.go.red{background:var(--r);color:#fff}#ab{padding:10px;border:0;border-radius:8px;background:var(--p2);font:inherit}
</style></head><body>
<header><b>BunkerNet</b><span id="st">connecting…</span><button id="who" title="Change your name"></button></header>
<div id="al"></div>
<nav><button data-t="chat" class="on">Chat</button><button data-t="data">Data</button><button data-t="nodes">Nodes</button></nav>
<main id="chat" class="on"><div id="convs"></div><div id="msgs"></div>
<form id="f"><button type="button" id="ab" title="Emergency alert">🚨</button><input id="x" autocomplete="off" placeholder="Message…"><small id="cnt"></small><button class="go" id="sb">Send</button></form></main>
<main id="data"><table><thead><tr><th>Key</th><th>Value</th><th>From</th><th>Updated</th></tr></thead><tbody id="dt"></tbody></table>
<form id="df"><input id="dk" placeholder="key" style="flex:.6"><input id="dv" placeholder="value"><button class="go">Set</button></form></main>
<main id="nodes"><p id="ni" style="color:var(--m)"></p><table><thead><tr><th>Node</th><th>Hops</th><th>Seen</th></tr></thead><tbody id="nt"></tbody></table></main>
<div id="nm" hidden><h2>Welcome to BunkerNet</h2><input id="ni2" placeholder="Your name" style="flex:none;width:240px"><button class="go" id="nb">Join</button><button class="chip" id="nc" hidden>Cancel</button><p style="color:var(--m);font-size:12px;max-width:290px;text-align:center">If this pop-up keeps forgetting your name, open http://192.168.4.1 in your normal browser.</p></div>
<script>
const $=s=>document.querySelector(s),E=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const ls=(k,v)=>{try{if(v===undefined)return localStorage.getItem(k);localStorage.setItem(k,v)}catch(e){return null}};
const rh=n=>{const a=new Uint8Array(n);try{crypto.getRandomValues(a)}catch(e){for(let i=0;i<n;i++)a[i]=Math.random()*256}return Array.from(a,b=>('0'+b.toString(16)).slice(-2)).join('')};
let me=ls('uid'),tok=ls('tok');
function newIdentity(){do{me=rh(4)}while(me=='00000000');tok=rh(16);ls('uid',me);ls('tok',tok)}
if(!me||!tok)newIdentity();
const bl=s=>{try{return new TextEncoder().encode(s).length}catch(e){return s.length}};
function clamp(el,max){el.addEventListener('input',()=>{let v=el.value;if(bl(v)>max){const a=Array.from(v);while(bl(a.join(''))>max)a.pop();el.value=a.join('')}if(el.id=='x')cnt()})}
function cnt(){const n=bl($('#x').value);$('#cnt').textContent=n>110?n+'/176':''}
let lastAct=Date.now();
let name=ls('name')||'',since=0,dv=-1,av=-1,alerts=[],seenAl={},hidAl={},alertMode=0,msgs=[],acked=new Set(),users=[],unread={},cur='all',busy=0,tab='chat',node='',seenM=new Set(),contacts={};
try{contacts=JSON.parse(ls('contacts')||'{}')}catch(e){}
const online=id=>users.some(u=>u.id==id);
const uname=id=>{const u=users.find(u=>u.id==id);return u?u.n:(contacts[id]||id.slice(0,4))};
const ph=()=>alertMode?'Emergency alert text…':(cur!='all'&&!online(cur)?uname(cur)+' is offline: will be stored':'Message…');
function askName(edit){if(name&&!edit)return;$('#ni2').value=name;$('#nc').hidden=!name;$('#nm').hidden=0;$('#ni2').focus()}
function who(){$('#who').textContent='👤 '+(name||'?')}
$('#who').onclick=()=>askName(1);$('#nc').onclick=()=>{$('#nm').hidden=1};
$('#nb').onclick=()=>{const v=$('#ni2').value.trim();if(!v)return;name=v;ls('name',v);$('#nm').hidden=1;who();poll()};
const p2=n=>('0'+n).slice(-2),tm=ms=>{const d=new Date(ms);return p2(d.getHours())+':'+p2(d.getMinutes())},
dayk=ms=>{const d=new Date(ms);return d.getFullYear()+'-'+d.getMonth()+'-'+d.getDate()},
dlab=ms=>new Date(ms).toLocaleDateString(undefined,{weekday:'short',day:'numeric',month:'short'}),
stamp=ms=>dayk(ms)==dayk(Date.now())?tm(ms):dlab(ms)+' '+tm(ms);
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{tab=b.dataset.t;document.querySelectorAll('nav button,main').forEach(e=>e.classList.remove('on'));b.classList.add('on');$('#'+tab).classList.add('on');if(tab=='nodes')nodes()});
const key=m=>m.t==1?'all':(m.f==me?m.to:m.f);
function render(){
 let h='<button class="chip'+(cur=='all'?' on':'')+'" data-c="all">Everyone'+(unread.all?'<i>'+unread.all+'</i>':'')+'</button>';
 const peers={};Object.keys(contacts).forEach(id=>peers[id]={id,n:contacts[id],off:1});users.forEach(u=>peers[u.id]={id:u.id,n:u.n,off:0});delete peers[me];
 Object.values(peers).forEach(u=>h+='<button class="chip'+(u.off?' off':'')+(cur==u.id?' on':'')+'" data-c="'+u.id+'">'+E(u.n)+(unread[u.id]?'<i>'+unread[u.id]+'</i>':'')+'</button>');
 $('#convs').innerHTML=h;
 $('#convs').querySelectorAll('.chip').forEach(c=>c.onclick=()=>{cur=c.dataset.c;unread[cur]=0;render()});
 const box=$('#msgs'),stick=box.scrollTop+box.clientHeight>=box.scrollHeight-40;
 let ld='';$('#x').placeholder=ph();
 box.innerHTML=msgs.filter(m=>key(m)==cur).sort((a,b)=>a.ms-b.ms).map(m=>{const mine=m.f==me,dk=dayk(m.ms);let h='';if(dk!=ld){ld=dk;h='<div class="day">'+dlab(m.ms)+'</div>'}
  return h+'<div class="m'+(mine?' me':'')+'">'+(mine?'':'<small>'+E(m.n)+' · '+tm(m.ms)+'</small>')+E(m.x)+(mine?'<s class="t">'+tm(m.ms)+'</s>'+(m.t==2?'<s class="'+(acked.has(m.m)?'ok">✓✓':'">'+(online(m.to)?'✓':'⏳ stored'))+'</s>':''):'')+'</div>'}).join('');
 if(stick)box.scrollTop=box.scrollHeight}
async function poll(){if(busy||!name)return;busy=1;
 try{const r=await fetch('/poll?since='+since+'&me='+me+'&tok='+tok+'&name='+encodeURIComponent(name)+'&dv='+dv+'&av='+av,{cache:'no-store'});
  if(r.status==403){newIdentity();since=0;seenM.clear();msgs=[];busy=0;return}   // id claimed by someone else: take a fresh identity
  const j=await r.json();
  if(node&&j.node!=node){node=j.node;since=0;busy=0;return}   // roamed to another node: its log numbering differs, resync
  node=j.node;users=j.users;since=j.last;
  users.forEach(u=>{if(u.id!=me&&contacts[u.id]!=u.n){contacts[u.id]=u.n;ls('contacts',JSON.stringify(contacts))}});
  j.msgs.forEach(m=>{if(m.t==5){acked.add(m.m);return}if(seenM.has(m.m))return;seenM.add(m.m);lastAct=Date.now();m.ms=m.s?m.s*1000:Date.now();msgs.push(m);const k=key(m);if(k!=cur&&m.f!=me)unread[k]=(unread[k]||0)+1});
  if(msgs.length>300)msgs=msgs.slice(-300);
  if(j.data){dv=j.dv;$('#dt').innerHTML=j.data.map(d=>'<tr><td>'+E(d.k)+'</td><td>'+E(d.v)+'</td><td>'+E(uname(d.f))+'</td><td>'+(d.s?stamp(d.s*1000):'–')+'</td></tr>').join('')||'<tr><td colspan=4 style="color:var(--m)">No data yet</td></tr>'}
  if(j.alerts){av=j.av;alerts=j.alerts;let nw=0;alerts.forEach(a=>{if(!seenAl[a.m]){seenAl[a.m]=1;nw=1}});if(nw){try{navigator.vibrate([400,150,400,150,400])}catch(e){}beep()}drawAl()}
  $('#st').className='';$('#st').textContent='node '+node+' · '+users.length+' online';render()
 }catch(e){$('#st').className='bad';$('#st').textContent='reconnecting…'}busy=0}
function beep(){try{const c=new(window.AudioContext||window.webkitAudioContext)(),o=c.createOscillator();o.type='square';o.frequency.value=880;o.connect(c.destination);o.start();o.stop(c.currentTime+.6)}catch(e){}}
function drawAl(){$('#al').innerHTML=alerts.filter(a=>!hidAl[a.m]).map(a=>'<div class="al"><span>🚨 '+E(a.n)+': '+E(a.x)+'</span><button data-h="'+a.m+'">OK</button><button data-c="'+a.m+'">Clear all</button></div>').join('');
 $('#al').querySelectorAll('button').forEach(b=>b.onclick=()=>{if(b.dataset.h){hidAl[b.dataset.h]=1;drawAl()}else{post('/alertclear',{me,m:b.dataset.c})}})}
$('#ab').onclick=()=>{alertMode=!alertMode;$('#sb').className='go'+(alertMode?' red':'');$('#sb').textContent=alertMode?'ALERT':'Send';$('#x').placeholder=ph()};
async function post(u,o){o.tok=tok;lastAct=Date.now();try{await fetch(u,{method:'POST',body:new URLSearchParams(o)})}catch(e){}poll()}
$('#f').onsubmit=e=>{e.preventDefault();const x=$('#x').value.trim();if(!x)return;$('#x').value='';if(alertMode){$('#ab').click();post('/alert',{me,name,x})}else post('/send',{me,name,ts:Math.floor(Date.now()/1000),to:cur=='all'?'0':cur,x})};
$('#df').onsubmit=e=>{e.preventDefault();const k=$('#dk').value.trim(),v=$('#dv').value.trim();if(!k)return;$('#dk').value=$('#dv').value='';post('/put',{me,name,ts:Math.floor(Date.now()/1000),k,v})};
async function nodes(){try{const j=await(await fetch('/nodes',{cache:'no-store'})).json();
 $('#ni').textContent='This node: '+j.id+' · up '+j.up+'s · free heap '+j.heap+' · clients '+j.clients+(j.defkey?' · ⚠ default mesh key in use, anyone can read the mesh':'');$('#ni').style.color=j.defkey?'var(--r)':'';
 $('#nt').innerHTML=j.nodes.map(n=>'<tr><td><span class="d"></span>'+E(n.n)+' <small style="color:var(--m)">'+n.id+'</small></td><td>'+n.h+'</td><td>'+n.a+'s</td></tr>').join('')}catch(e){}}
clamp($('#x'),176);clamp($('#dk'),23);clamp($('#dv'),47);clamp($('#ni2'),23);
['click','keydown','touchstart'].forEach(e=>addEventListener(e,()=>{lastAct=Date.now()},{passive:true}));
document.addEventListener('visibilitychange',()=>{if(!document.hidden)poll()});
/* poll gently: 1.5 s while active, slower when idle or the tab is hidden */
function sched(){setTimeout(async()=>{await poll();if(tab=='nodes'&&!document.hidden)nodes();sched()},document.hidden?8000:(Date.now()-lastAct>30000?3500:1500))}
sched();who();askName();poll();
</script></body></html>)HTML";
