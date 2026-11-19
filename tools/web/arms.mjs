// Web check of the one arm (tools/arms/refit_arms.py): in the browser build too, the Remington, the minigun and the
// flamethrower are given the grenade launcher's sleeve at load (their own files carry no picture of an arm), the launcher keeps its own, and
// each gun comes up without a page exception. A still of each is saved to look at.
// Drives a headless Chrome (sound off) over the DevTools protocol.
//   node tools/web/arms.mjs [base-url] [shot-prefix]      (serve build/web/dist first: node tools/web/serve.mjs)
import { spawn } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
const base = process.argv[2] || 'http://127.0.0.1:8642/', shot = process.argv[3] || 'build/web/arms';
const port = +(process.env.SMOKE_PORT || 9336), prof = process.cwd() + '/build/web/chrome-arms';
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
  for (const [slot, gun] of [[1, 'remington'], [2, 'minigun'], [5, 'flamethrower']]) {
    logs.length = 0;
    await send('Page.navigate', { url: base + `index.html?autostart=1&args=--debug,--god,--no-enemies,--weapon,${slot}` });
    await sleep(500);      // (the page before this one may still answer for a moment)
    const t0 = Date.now(); while (Date.now() - t0 < 120000 && !/"ev":"frame"/.test(await log())) await sleep(500);
    const loaded = {}; for (const l of (await log()).split('\n')) { const m = l.match(/"ev":"weapon","slot":(\d+),.*"bones":.*"sleeve":(-?\d+)/); if (m) loaded[m[1]] = +m[2]; }
    ok(loaded[slot] === 1, `${gun}: its arms are given the launcher's sleeve at load (sleeve: ${loaded[slot]})`);
    ok(loaded[0] === 0, `${gun} run: the launcher wears its own (sleeve: ${loaded[0]})`);
    await sleep(3000);
    const s = await send('Page.captureScreenshot', { format: 'png' }); if (s.data) writeFileSync(`${shot}-${gun}.png`, Buffer.from(s.data, 'base64'));
    const ex = logs.filter(l => l.startsWith('[exception]')); ok(ex.length === 0, `${gun}: no page exceptions` + (ex.length ? ': ' + ex[0] : ''));
  }
} catch (e) { console.log('FAIL ' + e.message); fails++; }
finally { try { chrome.kill(); } catch {} }
console.log(fails ? 'FAIL' : 'PASS'); process.exit(fails ? 1 : 0);
