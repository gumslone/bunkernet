// Checks qr.js by drawing codes and decoding them with Chrome's built-in barcode detector.
//   node tools/join-sheet/verify_qr.js      (needs Node 22+ and Google Chrome on macOS)
const http = require('http');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');

const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const PAGE = `<!doctype html><meta charset=utf-8><script src="/qr.js"></script><body>
<script>
window.run = async function (cases) {
  const det = new BarcodeDetector({ formats: ['qr_code'] });
  const out = [];
  for (const c of cases) {
    try {
      const q = QR.encode(c.text, c.mask === undefined ? {} : { mask: c.mask });
      const scale = 8, n = (q.size + 8) * scale, cv = document.createElement('canvas');
      cv.width = cv.height = n;
      const g = cv.getContext('2d'); g.fillStyle = '#fff'; g.fillRect(0, 0, n, n); g.fillStyle = '#000';
      for (let y = 0; y < q.size; y++) for (let x = 0; x < q.size; x++) if (q.get(x, y)) g.fillRect((x + 4) * scale, (y + 4) * scale, scale, scale);
      const r = await det.detect(cv);
      out.push({ ok: r.length === 1 && r[0].rawValue === c.text, v: q.version, mask: q.mask, len: c.text.length, got: r[0] && r[0].rawValue });
    } catch (e) { out.push({ ok: false, err: String(e), len: c.text.length }); }
  }
  return out;
};
</script>`;

async function main() {
  const qrSrc = fs.readFileSync(path.join(__dirname, 'qr.js'));
  const srv = http.createServer((req, res) => {
    if (req.url === '/qr.js') { res.setHeader('Content-Type', 'text/javascript'); return res.end(qrSrc); }
    res.setHeader('Content-Type', 'text/html'); res.end(PAGE);
  });
  await new Promise((r) => srv.listen(0, '127.0.0.1', r));
  const port = srv.address().port;

  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'bn-qr-'));
  const chrome = spawn(CHROME, ['--headless=new', '--remote-debugging-port=0', `--user-data-dir=${profile}`,
    '--no-first-run', `http://127.0.0.1:${port}/`], { stdio: 'ignore' });
  let cdp, targets;
  for (let i = 0; i < 100 && !targets; i++) {
    try {
      cdp = fs.readFileSync(path.join(profile, 'DevToolsActivePort'), 'utf8').split('\n')[0];
      targets = await (await fetch(`http://127.0.0.1:${cdp}/json`)).json();
    } catch { await sleep(300); }
  }
  if (!targets) throw new Error('Chrome did not start');
  await sleep(800);
  const page = targets.find((t) => t.type === 'page');
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r));
  const send = (method, params) => new Promise((res) => {
    const id = Math.floor(Math.random() * 1e9);
    const h = (e) => { const m = JSON.parse(e.data); if (m.id === id) { ws.removeEventListener('message', h); res(m); } };
    ws.addEventListener('message', h); ws.send(JSON.stringify({ id, method, params }));
  });

  const long106 = 'WIFI:T:WPA;S:BunkerNet-Bunker-Level-2;P:' + 'x'.repeat(106 - 'WIFI:T:WPA;S:BunkerNet-Bunker-Level-2;P:'.length - 2) + ';;';
  const cases = [
    { text: 'A' }, { text: 'http://192.168.4.1' }, { text: 'WIFI:T:nopass;S:BunkerNet;;' },
    { text: 'WIFI:T:WPA;S:BunkerNet;P:correct horse battery staple;;' },
    { text: 'WIFI:T:WPA;S:Бункер;P:секретний пароль 123;;' },
    { text: 'x'.repeat(14) }, { text: 'x'.repeat(26) }, { text: 'x'.repeat(42) },
    { text: 'y'.repeat(62) }, { text: 'z'.repeat(84) }, { text: long106 }, { text: 'q'.repeat(106) },
  ];
  for (let m = 0; m < 8; m++) { cases.push({ text: 'http://192.168.4.1', mask: m }); cases.push({ text: 'WIFI:T:WPA;S:BunkerNet;P:correct horse battery staple;;', mask: m }); }

  const r = await send('Runtime.evaluate', { expression: 'run(' + JSON.stringify(cases) + ')', awaitPromise: true, returnByValue: true });
  const results = r.result.result.value;
  let bad = 0;
  results.forEach((x, i) => {
    if (!x.ok) { bad++; console.log('FAIL', JSON.stringify(cases[i].text).slice(0, 60), 'mask', cases[i].mask, JSON.stringify(x)); }
  });
  console.log(`${results.length - bad}/${results.length} QR codes decoded correctly`);
  ws.close(); chrome.kill(); srv.close();
  await sleep(500);
  try { fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 }); } catch {}
  process.exit(bad ? 1 : 0);
}
main().catch((e) => { console.error(e); process.exit(1); });
