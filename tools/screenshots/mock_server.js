// Mock BunkerNet node for UI development and README screenshots.
// Serves the real embedded web page (from web_ui.h) with canned API data.
//   node tools/screenshots/mock_server.js [port]
const http = require('http');
const fs = require('fs');
const path = require('path');

const port = Number(process.argv[2] || 8080);
const src = fs.readFileSync(path.join(__dirname, '../../firmware/bunkernet/web_ui.h'), 'utf8');
const html = src.match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];

const ANNA = '0000a001', MARK = '0000b002', MAYA = '0000c003', SAM = '0000e005'; // Sam is offline
const state = { alert: false };

// f === 'me' is replaced by the requesting client's id
const msgs = [
  { i: 1, t: 1, f: ANNA, to: '00000000', m: '00000001', n: 'Anna', x: 'Water pump in section B is running again 💧', ago: 100000 },
  { i: 2, t: 1, f: MARK, to: '00000000', m: '00000002', n: 'Mark', x: 'Great. Who has the spare fuses?', ago: 99000 },
  { i: 3, t: 1, f: 'me', to: '00000000', m: '00000003', n: 'Olek', x: 'I do, bringing them to the workshop now', ago: 3000 },
  { i: 4, t: 1, f: MAYA, to: '00000000', m: '00000004', n: 'Maya', x: 'Radio check from the north tunnel: loud and clear, 3 hops', ago: 2400 },
  { i: 5, t: 1, f: ANNA, to: '00000000', m: '00000005', n: 'Anna', x: 'Thanks! Dinner at 19:00 in the common room', ago: 1500 },
  { i: 6, t: 2, f: MARK, to: 'me', m: '00000006', n: 'Mark', x: 'Can you check the generator fuel level?', ago: 1200 },
  { i: 7, t: 2, f: 'me', to: MARK, m: '00000007', n: 'Olek', x: 'On my way', ago: 900 },
  { i: 8, t: 2, f: 'me', to: MARK, m: '00000008', n: 'Olek', x: 'Fuel is at 70%, enough for 2 days', ago: 600 },
  { i: 9, t: 2, f: ANNA, to: 'me', m: '00000009', n: 'Anna', x: 'Are you bringing the fuses?', ago: 300 },
  { i: 11, t: 2, f: 'me', to: SAM, m: '0000000b', n: 'Olek', x: 'Filter instructions are taped to the fridge', ago: 240 },
  { i: 10, t: 5, f: MARK, to: 'me', m: '00000007', n: '', x: '', ago: 590 },
];
const users = [{ id: ANNA, n: 'Anna' }, { id: MARK, n: 'Mark' }, { id: MAYA, n: 'Maya' }];
const data = [
  { k: 'water_level', v: '62 %', f: MAYA, ago: 1800 },
  { k: 'fuel_main', v: '70 %', f: MARK, ago: 7200 },
  { k: 'temp_cellar', v: '14.2 C', f: ANNA, ago: 300 },
  { k: 'door_north', v: 'closed', f: MAYA, ago: 90000 },
  { k: 'air_filter_B', v: 'check', f: MARK, ago: 5400 },
];
const nodes = {
  id: 'A1B2', up: 86412, heap: 21840, clients: 3,
  nodes: [
    { id: 'A1B2', n: 'Node-A1B2', h: 0, a: 0 },
    { id: '77C4', n: 'Node-77C4', h: 1, a: 3 },
    { id: 'E019', n: 'Node-E019', h: 1, a: 6 },
    { id: '5D3F', n: 'Node-5D3F', h: 2, a: 8 },
    { id: '0B88', n: 'Node-0B88', h: 3, a: 2 },
  ],
};

function poll(q) {
  const me = q.get('me') || '00000000', since = Number(q.get('since') || 0);
  const now = Math.floor(Date.now() / 1000);
  const fix = (v) => (v === 'me' ? me : v);
  const out = {
    node: 'A1B2', last: msgs.length,
    msgs: msgs.filter((m) => m.i > since).map((m) => ({ ...m, s: now - m.ago, f: fix(m.f), to: fix(m.to) })),
    users: [...users, { id: me, n: q.get('name') || 'me' }],
    dv: 1, av: state.alert ? 2 : 1,
  };
  if (q.get('dv') !== '1') out.data = data.map((d) => ({ ...d, s: now - d.ago }));
  if (state.alert && q.get('av') !== '2') {
    out.alerts = [{ m: 'aaaa0001', n: 'Node-77C4', x: 'SOS button pressed at Node-77C4' }];
  }
  return out;
}

http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  const json = (o) => { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(o)); };
  if (u.pathname === '/') { res.setHeader('Content-Type', 'text/html'); return res.end(html); }
  if (u.pathname === '/poll') return json(poll(u.searchParams));
  if (u.pathname === '/nodes') return json(nodes);
  if (u.pathname === '/__alert') { state.alert = u.searchParams.get('on') === '1'; return res.end('ok'); }
  res.end('ok');
}).listen(port, '127.0.0.1', () => console.log(`mock BunkerNet node on http://127.0.0.1:${port}`));
