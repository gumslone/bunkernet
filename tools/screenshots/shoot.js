// Regenerates docs/screenshots/*.png from the real web UI + mock server using headless Chrome.
//   node tools/screenshots/shoot.js
// Needs Node 22+ and Google Chrome (set CHROME=/path/to/chrome to override). No npm deps.
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const OUT = path.join(__dirname, '../../docs/screenshots');
const MOCK = 8099, CDP = 9333;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function main() {
  fs.mkdirSync(OUT, { recursive: true });
  const mock = spawn('node', [path.join(__dirname, 'mock_server.js'), String(MOCK)], { stdio: 'ignore' });
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'bn-shot-'));
  const chrome = spawn(CHROME, [
    '--headless=new', `--remote-debugging-port=${CDP}`, `--user-data-dir=${profile}`,
    '--no-first-run', '--hide-scrollbars', 'about:blank',
  ], { stdio: 'ignore' });
  try {
    let targets;
    for (let i = 0; i < 100 && !targets; i++) {
      try { targets = await (await fetch(`http://127.0.0.1:${CDP}/json`)).json(); } catch { await sleep(300); }
    }
    if (!targets) throw new Error("Chrome remote debugging port did not come up");
    const page = targets.find((t) => t.type === 'page');
    const ws = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise((r) => (ws.onopen = r));
    let id = 0; const pending = new Map();
    ws.onmessage = (e) => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } };
    const cdp = (method, params = {}) => new Promise((res) => { const i = ++id; pending.set(i, res); ws.send(JSON.stringify({ id: i, method, params })); });
    const js = (expr) => cdp('Runtime.evaluate', { expression: expr, awaitPromise: true });
    const shot = async (name) => {
      await sleep(900);
      const r = await cdp('Page.captureScreenshot', { format: 'png' });
      fs.writeFileSync(path.join(OUT, name + '.png'), Buffer.from(r.result.data, 'base64'));
      console.log('wrote', name + '.png');
    };

    await cdp('Page.enable');
    await cdp('Emulation.setDeviceMetricsOverride', { width: 390, height: 640, deviceScaleFactor: 2, mobile: true });
    await cdp('Emulation.setEmulatedMedia', { features: [] });
    const open = async () => {
      await cdp('Page.navigate', { url: `http://127.0.0.1:${MOCK}/` });
      await sleep(600);
      await js("localStorage.setItem('uid','0000d004');localStorage.setItem('name','Olek');location.reload()");
      await sleep(1200);
    };

    await fetch(`http://127.0.0.1:${MOCK}/__alert?on=0`);
    await open();
    await shot('chat');

    await js("document.querySelector('.chip[data-c=\"0000b002\"]').click()");
    await shot('private');

    await js("document.querySelector('nav button[data-t=data]').click()");
    await shot('data');

    await js("document.querySelector('nav button[data-t=nodes]').click()");
    await shot('nodes');

    await fetch(`http://127.0.0.1:${MOCK}/__alert?on=1`);
    await open();
    await shot('alert');
    ws.close();
  } finally {
    chrome.kill(); mock.kill();
    await sleep(800);
    try { fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 }); } catch {}
  }
}
main().catch((e) => { console.error(e); process.exit(1); });
