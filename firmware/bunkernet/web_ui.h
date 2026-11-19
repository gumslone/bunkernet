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
.chip.on{background:var(--a);color:#001}.chip i{font-style:normal;background:var(--r);color:#fff;border-radius:8px;padding:0 5px;margin-left:5px;font-size:11px}
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
#al{background:var(--r);color:#fff}.al{display:flex;align-items:center;gap:8px;padding:8px 12px;border-bottom:1px solid #0004;font-weight:600}.al span{flex:1}.al button{border:0;border-radius:6px;padding:4px 8px;font:inherit;font-size:12px}
.go.red{background:var(--r);color:#fff}#ab{padding:10px;border:0;border-radius:8px;background:var(--p2);font:inherit}
</style></head><body>
<header><b>BunkerNet</b><span id="st">connecting…</span></header>
<div id="al"></div>
<nav><button data-t="chat" class="on">Chat</button><button data-t="data">Data</button><button data-t="nodes">Nodes</button></nav>
<main id="chat" class="on"><div id="convs"></div><div id="msgs"></div>
<form id="f"><button type="button" id="ab" title="Emergency alert">🚨</button><input id="x" maxlength="140" autocomplete="off" placeholder="Message…"><button class="go" id="sb">Send</button></form></main>
<main id="data"><table><thead><tr><th>Key</th><th>Value</th><th>From</th></tr></thead><tbody id="dt"></tbody></table>
<form id="df"><input id="dk" maxlength="15" placeholder="key" style="flex:.6"><input id="dv" maxlength="47" placeholder="value"><button class="go">Set</button></form></main>
<main id="nodes"><p id="ni" style="color:var(--m)"></p><table><thead><tr><th>Node</th><th>Hops</th><th>Seen</th></tr></thead><tbody id="nt"></tbody></table></main>
<div id="nm" hidden><h2>Welcome to BunkerNet</h2><input id="ni2" maxlength="15" placeholder="Your name" style="flex:none;width:240px"><button class="go" id="nb">Join</button></div>
<script>
const $=s=>document.querySelector(s),E=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const ls=(k,v)=>{try{if(v===undefined)return localStorage.getItem(k);localStorage.setItem(k,v)}catch(e){return null}};
let me=ls('uid');if(!me){me=('0000000'+((Math.random()*4294967294+1)>>>0).toString(16)).slice(-8);ls('uid',me)}
let name=ls('name')||'',since=0,dv=-1,av=-1,alerts=[],seenAl={},hidAl={},alertMode=0,msgs=[],acked=new Set(),users=[],unread={},cur='all',busy=0,tab='chat',node='';
const uname=id=>{const u=users.find(u=>u.id==id);return u?u.n:id.slice(0,4)};
function askName(){if(name)return;$('#nm').hidden=0}
$('#nb').onclick=()=>{const v=$('#ni2').value.trim();if(!v)return;name=v;ls('name',v);$('#nm').hidden=1;poll()};
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{tab=b.dataset.t;document.querySelectorAll('nav button,main').forEach(e=>e.classList.remove('on'));b.classList.add('on');$('#'+tab).classList.add('on');if(tab=='nodes')nodes()});
const key=m=>m.t==1?'all':(m.f==me?m.to:m.f);
function render(){
 let h='<button class="chip'+(cur=='all'?' on':'')+'" data-c="all">Everyone'+(unread.all?'<i>'+unread.all+'</i>':'')+'</button>';
 users.filter(u=>u.id!=me).forEach(u=>h+='<button class="chip'+(cur==u.id?' on':'')+'" data-c="'+u.id+'">'+E(u.n)+(unread[u.id]?'<i>'+unread[u.id]+'</i>':'')+'</button>');
 $('#convs').innerHTML=h;
 $('#convs').querySelectorAll('.chip').forEach(c=>c.onclick=()=>{cur=c.dataset.c;unread[cur]=0;render()});
 const box=$('#msgs'),stick=box.scrollTop+box.clientHeight>=box.scrollHeight-40;
 box.innerHTML=msgs.filter(m=>key(m)==cur).map(m=>{const mine=m.f==me;
  return '<div class="m'+(mine?' me':'')+'">'+(mine?'':'<small>'+E(m.n)+'</small>')+E(m.x)+(mine&&m.t==2?'<s class="'+(acked.has(m.m)?'ok">✓✓':'">✓')+'</s>':'')+'</div>'}).join('');
 if(stick)box.scrollTop=box.scrollHeight}
async function poll(){if(busy||!name)return;busy=1;
 try{const r=await fetch('/poll?since='+since+'&me='+me+'&name='+encodeURIComponent(name)+'&dv='+dv+'&av='+av,{cache:'no-store'});const j=await r.json();
  node=j.node;users=j.users;since=j.last;
  j.msgs.forEach(m=>{if(m.t==5){acked.add(m.m);return}msgs.push(m);const k=key(m);if(k!=cur&&m.f!=me)unread[k]=(unread[k]||0)+1});
  if(msgs.length>300)msgs=msgs.slice(-300);
  if(j.data){dv=j.dv;$('#dt').innerHTML=j.data.map(d=>'<tr><td>'+E(d.k)+'</td><td>'+E(d.v)+'</td><td>'+E(uname(d.f))+'</td></tr>').join('')||'<tr><td colspan=3 style="color:var(--m)">No data yet</td></tr>'}
  if(j.alerts){av=j.av;alerts=j.alerts;let nw=0;alerts.forEach(a=>{if(!seenAl[a.m]){seenAl[a.m]=1;nw=1}});if(nw){try{navigator.vibrate([400,150,400,150,400])}catch(e){}beep()}drawAl()}
  $('#st').className='';$('#st').textContent='node '+node+' · '+users.length+' online';render()
 }catch(e){$('#st').className='bad';$('#st').textContent='reconnecting…'}busy=0}
function beep(){try{const c=new(window.AudioContext||window.webkitAudioContext)(),o=c.createOscillator();o.type='square';o.frequency.value=880;o.connect(c.destination);o.start();o.stop(c.currentTime+.6)}catch(e){}}
function drawAl(){$('#al').innerHTML=alerts.filter(a=>!hidAl[a.m]).map(a=>'<div class="al"><span>🚨 '+E(a.n)+': '+E(a.x)+'</span><button data-h="'+a.m+'">OK</button><button data-c="'+a.m+'">Clear all</button></div>').join('');
 $('#al').querySelectorAll('button').forEach(b=>b.onclick=()=>{if(b.dataset.h){hidAl[b.dataset.h]=1;drawAl()}else{post('/alertclear',{me,m:b.dataset.c})}})}
$('#ab').onclick=()=>{alertMode=!alertMode;$('#sb').className='go'+(alertMode?' red':'');$('#sb').textContent=alertMode?'ALERT':'Send';$('#x').placeholder=alertMode?'Emergency alert text…':'Message…'};
async function post(u,o){try{await fetch(u,{method:'POST',body:new URLSearchParams(o)})}catch(e){}poll()}
$('#f').onsubmit=e=>{e.preventDefault();const x=$('#x').value.trim();if(!x)return;$('#x').value='';if(alertMode){$('#ab').click();post('/alert',{me,name,x})}else post('/send',{me,name,to:cur=='all'?'0':cur,x})};
$('#df').onsubmit=e=>{e.preventDefault();const k=$('#dk').value.trim(),v=$('#dv').value.trim();if(!k)return;$('#dk').value=$('#dv').value='';post('/put',{me,name,k,v})};
async function nodes(){try{const j=await(await fetch('/nodes',{cache:'no-store'})).json();
 $('#ni').textContent='This node: '+j.id+' · up '+j.up+'s · free heap '+j.heap+' · clients '+j.clients;
 $('#nt').innerHTML=j.nodes.map(n=>'<tr><td><span class="d"></span>'+E(n.n)+' <small style="color:var(--m)">'+n.id+'</small></td><td>'+n.h+'</td><td>'+n.a+'s</td></tr>').join('')}catch(e){}}
setInterval(()=>{poll();if(tab=='nodes')nodes()},1500);askName();poll();
</script></body></html>)HTML";
