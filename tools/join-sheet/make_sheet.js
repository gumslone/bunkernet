// Makes a printable one-page "how to join" sheet with Wi-Fi and app QR codes.
//   node tools/join-sheet/make_sheet.js [--ssid BunkerNet] [--password SECRET]
//                                       [--lang en|uk|all] [--out docs]
// Writes join-sheet-<lang>.html and, if Google Chrome is found, join-sheet-<lang>.pdf.
// Print it and put it next to the nodes. Needs Node 18+ (no npm packages).
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const QR = require('./qr.js');

const args = Object.fromEntries(process.argv.slice(2).reduce((a, x, i, all) => (x.startsWith('--') ? [...a, [x.slice(2), all[i + 1]]] : a), []));
const SSID = args.ssid || 'BunkerNet';
const PASSWORD = args.password || '';
const OUT = path.resolve(args.out || path.join(__dirname, '../../docs'));
const LANGS = args.lang && args.lang !== 'all' ? [args.lang] : ['en', 'uk'];
const URL = 'http://192.168.4.1';
const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';

const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const wifiEsc = (s) => s.replace(/([\\;,:"])/g, '\\$1');
const wifiPayload = PASSWORD ? `WIFI:T:WPA;S:${wifiEsc(SSID)};P:${wifiEsc(PASSWORD)};;` : `WIFI:T:nopass;S:${wifiEsc(SSID)};;`;

const T = {
  en: {
    lang: 'en', title: 'Local network, no internet needed',
    sub: 'Chat, private messages, shared data and emergency alerts that work when the internet does not.',
    s1: 'Join the Wi-Fi', s1t: 'Scan the first code, or open your phone\'s Wi-Fi settings and choose',
    pw: 'Password:', open: '(no password)',
    s2: 'Open the app', s2t: 'A page opens by itself. If not, scan the second code or type',
    s2n: 'in your normal browser (Chrome, Safari). If a small pop-up window keeps forgetting your name, use the normal browser instead.',
    s3: 'Choose a name', s3t: 'Type a name when asked. Tap your name at the top right to change it later.',
    howTitle: 'How to use it',
    how: [
      ['Chat tab', '"Everyone" is the group chat. Tap a person\'s name to send them a private message.'],
      ['✓ ✓✓ ⏳', '✓ sent. ✓✓ delivered. ⏳ stored: it is delivered the moment that person is back on the network.'],
      ['🚨 button', 'Raises a loud red alert on every device. Only for real emergencies. Anyone can clear it.'],
      ['Data tab', 'Shared notes and values for everyone, such as water level or fuel.'],
      ['Nodes tab', 'Shows which relay boxes are working and how far away they are.'],
    ],
    goodTitle: 'Good to know',
    good: ['There is no internet. Only people on this network can see your messages.', 'Messages are short text, up to about 176 letters.', 'Times come from each person\'s own phone clock.', 'Relay boxes keep messages for offline people while they have power.'],
    fill: ['Relay boxes are at', 'Wi-Fi password', 'Contact person'],
    scan1: 'Scan to join Wi-Fi', scan2: 'Scan to open the app',
  },
  uk: {
    lang: 'uk', title: 'Локальна мережа, інтернет не потрібен',
    sub: 'Чат, приватні повідомлення, спільні дані та сигнали тривоги, які працюють, коли інтернету немає.',
    s1: 'Підключіться до Wi-Fi', s1t: 'Скануйте перший код або відкрийте налаштування Wi-Fi на телефоні й виберіть мережу',
    pw: 'Пароль:', open: '(без пароля)',
    s2: 'Відкрийте застосунок', s2t: 'Сторінка відкриється сама. Якщо ні, скануйте другий код або введіть',
    s2n: 'у звичайному браузері (Chrome, Safari). Якщо маленьке вікно постійно забуває ваше ім\'я, користуйтеся звичайним браузером.',
    s3: 'Виберіть ім\'я', s3t: 'Введіть ім\'я, коли з\'явиться запит. Щоб змінити його згодом, торкніться свого імені вгорі праворуч.',
    howTitle: 'Як користуватися',
    how: [
      ['Вкладка Chat', '«Everyone» це спільний чат. Торкніться імені людини, щоб написати їй приватно.'],
      ['✓ ✓✓ ⏳', '✓ надіслано. ✓✓ доставлено. ⏳ збережено: дійде, щойно ця людина з\'явиться в мережі.'],
      ['Кнопка 🚨', 'Вмикає гучний червоний сигнал на всіх пристроях. Лише у разі справжньої небезпеки. Скасувати може будь-хто.'],
      ['Вкладка Data', 'Спільні нотатки й значення для всіх, наприклад рівень води чи палива.'],
      ['Вкладка Nodes', 'Показує, які вузли зв\'язку працюють і як далеко вони.'],
    ],
    goodTitle: 'Варто знати',
    good: ['Інтернету немає. Ваші повідомлення бачать лише люди в цій мережі.', 'Повідомлення короткі, до приблизно 176 літер.', 'Час береться з годинника на телефоні кожного.', 'Вузли зв\'язку зберігають повідомлення для тих, кого немає в мережі, поки є живлення.'],
    fill: ['Вузли зв\'язку розташовані', 'Пароль Wi-Fi', 'Відповідальна особа'],
    scan1: 'Скануйте, щоб підключитися до Wi-Fi', scan2: 'Скануйте, щоб відкрити застосунок',
  },
};

function html(t) {
  return `<!doctype html>
<html lang="${t.lang}"><head><meta charset="utf-8"><title>BunkerNet</title>
<style>
@page { size: A4; margin: 11mm; }
* { box-sizing: border-box; }
body { margin: 0; font: 11.5pt/1.3 -apple-system, "Segoe UI", Helvetica, Arial, sans-serif; color: #111; }
header { border-bottom: 3px solid #111; padding-bottom: 6px; margin-bottom: 10px; }
h1 { margin: 0; font-size: 30pt; letter-spacing: .5px; }
header p { margin: 2px 0 0; font-size: 12.5pt; color: #333; }
.top { display: grid; grid-template-columns: 1fr 50mm; gap: 12px; }
.step { display: flex; gap: 10px; margin-bottom: 9px; }
.num { flex: none; width: 28px; height: 28px; border-radius: 50%; background: #111; color: #fff; font-weight: 700; display: flex; align-items: center; justify-content: center; font-size: 14pt; margin-top: 2px; }
.step h2 { margin: 0; font-size: 14pt; } .step p { margin: 1px 0 0; }
code, .tag { font-family: ui-monospace, Menlo, monospace; background: #eee; padding: 1px 6px; border-radius: 4px; font-weight: 600; }
.qrs { display: flex; flex-direction: column; gap: 8px; }
.qr { border: 2px solid #111; border-radius: 10px; padding: 6px; text-align: center; }
.qr svg { width: 100%; height: auto; display: block; }
.qr b { display: block; font-size: 10.5pt; line-height: 1.2; }
h3 { margin: 9px 0 4px; font-size: 14pt; border-bottom: 1px solid #999; padding-bottom: 2px; }
table { border-collapse: collapse; width: 100%; }
td { padding: 3px 6px; vertical-align: top; border-bottom: 1px solid #ddd; }
td:first-child { width: 31mm; font-weight: 700; white-space: nowrap; }
ul { margin: 4px 0 0 18px; padding: 0; } li { margin: 2px 0; }
.fill { display: grid; grid-template-columns: repeat(3, 1fr); gap: 14px; margin-top: 8px; }
.fill div { border-bottom: 1.5px solid #111; padding-top: 16px; font-size: 10.5pt; color: #333; }
</style></head><body>
<header><h1>BunkerNet</h1><p><b>${esc(t.title)}.</b> ${esc(t.sub)}</p></header>
<div class="top">
 <div>
  <div class="step"><div class="num">1</div><div><h2>${esc(t.s1)}</h2>
   <p>${esc(t.s1t)} <code>${esc(SSID)}</code>. ${PASSWORD ? esc(t.pw) + ' <code>' + esc(PASSWORD) + '</code>' : esc(t.open)}</p></div></div>
  <div class="step"><div class="num">2</div><div><h2>${esc(t.s2)}</h2>
   <p>${esc(t.s2t)} <code>192.168.4.1</code> ${esc(t.s2n)}</p></div></div>
  <div class="step"><div class="num">3</div><div><h2>${esc(t.s3)}</h2><p>${esc(t.s3t)}</p></div></div>
 </div>
 <div class="qrs">
  <div class="qr">${QR.svg(wifiPayload)}<b>${esc(t.scan1)}</b></div>
  <div class="qr">${QR.svg(URL)}<b>${esc(t.scan2)}</b></div>
 </div>
</div>
<h3>${esc(t.howTitle)}</h3>
<table>${t.how.map(([a, b]) => `<tr><td>${esc(a)}</td><td>${esc(b)}</td></tr>`).join('')}</table>
<h3>${esc(t.goodTitle)}</h3>
<ul>${t.good.map((g) => `<li>${esc(g)}</li>`).join('')}</ul>
<div class="fill">${t.fill.map((f) => `<div>${esc(f)}</div>`).join('')}</div>
</body></html>`;
}

fs.mkdirSync(OUT, { recursive: true });
for (const code of LANGS) {
  const t = T[code];
  if (!t) { console.error('unknown language', code); process.exit(1); }
  const file = path.join(OUT, `join-sheet-${code}.html`);
  fs.writeFileSync(file, html(t));
  console.log('wrote', file);
  if (fs.existsSync(CHROME)) {
    const pdf = file.replace(/\.html$/, '.pdf');
    const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'bn-pdf-'));
    const r = spawnSync(CHROME, ['--headless=new', '--disable-gpu', `--user-data-dir=${profile}`, '--no-pdf-header-footer', `--print-to-pdf=${pdf}`, 'file://' + file], { stdio: 'ignore', timeout: 60000 });
    try { fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 }); } catch {}
    console.log(fs.existsSync(pdf) ? 'wrote ' + pdf : 'PDF failed (exit ' + r.status + ')');
  } else {
    console.log('Chrome not found, open the HTML file in a browser and print it (A4, no headers).');
  }
}
