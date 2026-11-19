// Web check of the chunks a blast leaves (src/gibs.h): in the browser build too they are loaded with the guns and drawn
// through the lit shader, a burst throws thirteen and all of them come to rest lying on the blood, without a page exception. A still is
// saved to look at.
// Drives a headless Chrome (sound off) over the DevTools protocol.
//   node tools/web/gibs.mjs [base-url] [shot.png]      (serve build/web/dist first: node tools/web/serve.mjs)
import { spawn } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
const base = process.argv[2] || 'http://127.0.0.1:8642/', shot = process.argv[3] || 'build/web/gibs.png';
const port = +(process.env.SMOKE_PORT || 9337), prof = process.cwd() + '/build/web/chrome-gibs';
mkdirSync(prof, { recursive: true });
const chrome = spawn('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', ['--headless=new', '--no-first-run',
  '--disable-extensions', `--user-data-dir=${prof}`, '--window-size=1280,720', `--remote-debugging-port=${port}`, '--mute-audio',
  '--autoplay-policy=no-user-gesture-required', '--enable-unsafe-swiftshader', 'about:blank'], { stdio: 'ignore' });
const sleep = ms => new Promise(r => setTimeout(r, ms));
let ws, id = 0, fails = 0; const pending = new Map(), logs = [];
const send = (method, params = {}) => new Promise(res => { const i = ++id; pending.set(i, res); ws.send(JSON.stringify({ id: i, method, params })); });
const ok = (c, what) => { console.log((c ? 'ok   ' : 'FAIL ') + what); if (!c) fails++; };
try {
  let target;
  for (let k = 0; k < 50 && !target; k++) { await sleep(200); try { target = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find(t => t.type === 'page'); } catch {} }
  if (!target) throw new Error('chrome did not start');
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise(r => ws.addEventListener('open', r));
  ws.addEventListener('message', ev => { const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m.result || m.error); pending.delete(m.id); }
    else if (m.method === 'Runtime.exceptionThrown') logs.push('[exception] ' + (m.params.exceptionDetails.exception?.description || m.params.exceptionDetails.text)); });
  await send('Runtime.enable'); await send('Page.enable');
  await send('Emulation.setFocusEmulationEnabled', { enabled: true }); await send('Page.bringToFront');
  const ev = async e => (await send('Runtime.evaluate', { expression: e, returnByValue: true })).result?.value;
  const log = () => ev(`(function(){ try { return FS.readFile('chernobyl2-debug.log', { encoding: 'utf8' }); } catch (e) { return ''; } })()`).then(s => s || '');
  const lines = async name => (await log()).split('\n').filter(l => l.includes(`"ev":"${name}"`)).map(l => { try { return JSON.parse(l); } catch { return {}; } });
  await send('Page.navigate', { url: base + 'index.html?autostart=1&args=--debug,--god,--no-enemies,--look,180,-32,--gibs,60' });
  const t0 = Date.now(); while (Date.now() - t0 < 120000 && !/"ev":"frame"/.test(await log())) await sleep(500);
  const g = (await lines('gibs'))[0] || {};
  ok(g.meshes === 8 && g.tex === 3 && g.lit === 1, `the chunks are loaded and lit: ${JSON.stringify(g)}`);
  const t1 = Date.now(); let rest = [];
  while (Date.now() - t1 < 90000 && (rest = await lines('gibrest')).length < 13) await sleep(1000);
  const b = (await lines('gibburst'))[0] || {};
  ok(b.n === 13, `a burst throws thirteen: ${JSON.stringify(b)}`);
  const lows = rest.map(r => r.low);
  ok(rest.length === 13 && rest.every(r => r.low >= 0.0145 && r.low <= 0.0165), `and all of them come to rest level, on the blood: the lowest point of each, as drawn, ${(Math.min(...lows) * 1000).toFixed(1)} to ${(Math.max(...lows) * 1000).toFixed(1)} mm above its floor (14.5 to 16.5; ${rest.length} at rest)`);
  const s = await send('Page.captureScreenshot', { format: 'png' }); if (s.data) writeFileSync(shot, Buffer.from(s.data, 'base64'));
  const ex = logs.filter(l => l.startsWith('[exception]')); ok(ex.length === 0, 'no page exceptions' + (ex.length ? ': ' + ex[0] : ''));
} catch (e) { console.log('FAIL ' + e.message); fails++; }
finally { try { chrome.kill(); } catch {} }
console.log(fails ? 'FAIL' : 'PASS'); process.exit(fails ? 1 : 0);
