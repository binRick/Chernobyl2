// Web check of weapon 5, the claymore (src/mines.h): the browser build loads it with the guns -- the hand model, the
// floor model (one mesh) and its sheet of three textures, which the pack carries as .jpg (tools/web/pack_web.py); the
// hand model takes its two from what is already loaded -- and 5 takes it out, three carried.
// Drives a headless Chrome (sound off) over the DevTools protocol; keys go to that page only.
//   node tools/web/claymore.mjs [base-url] [shot.png]      (serve build/web/dist first: node tools/web/serve.mjs)
import { spawn } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
const base = process.argv[2] || 'http://127.0.0.1:8642/', shot = process.argv[3] || 'build/web/claymore.png';
const port = +(process.env.SMOKE_PORT || 9335), prof = process.cwd() + '/build/web/chrome-claymore';
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
  await send('Page.navigate', { url: base + 'index.html?autostart=1&args=--debug,--god,--no-enemies' });
  const ev = async e => (await send('Runtime.evaluate', { expression: e, returnByValue: true })).result?.value;
  const log = () => ev(`(function(){ try { return FS.readFile('chernobyl2-debug.log', { encoding: 'utf8' }); } catch (e) { return ''; } })()`).then(s => s || '');
  const t0 = Date.now(); while (Date.now() - t0 < 120000 && !/"ev":"frame"/.test(await log())) await sleep(500);
  const l0 = await log();
  const m = l0.match(/\{[^\n]*"ev":"mines"[^\n]*\}/);
  ok(m && /"vm":1,"world":1/.test(m[0]) && /"meshes":1,"tex":3,"vmTex":2/.test(m[0]), 'loaded with the guns: ' + (m ? m[0] : 'no "mines" line'));
  await sleep(1500);
  await send('Input.dispatchKeyEvent', { type: 'keyDown', key: '5', code: 'Digit5', windowsVirtualKeyCode: 53, text: '5' }); await sleep(150);   // held across a frame: the game polls keys per frame
  await send('Input.dispatchKeyEvent', { type: 'keyUp', key: '5', code: 'Digit5', windowsVirtualKeyCode: 53 });
  await sleep(1500);
  const p = (await log()).match(/\{[^\n]*"pick":[^\n]*\}/);
  ok(p && /"pick":1,"left":3/.test(p[0]), '5 takes it out: ' + (p ? p[0] : 'no pick logged'));
  const s = await send('Page.captureScreenshot', { format: 'png' }); if (s.data) writeFileSync(shot, Buffer.from(s.data, 'base64'));
  const ex = logs.filter(l => l.startsWith('[exception]')); ok(ex.length === 0, 'no page exceptions' + (ex.length ? ': ' + ex[0] : ''));
} catch (e) { console.log('FAIL ' + e.message); fails++; }
finally { try { chrome.kill(); } catch {} }
console.log(fails ? 'FAIL' : 'PASS'); process.exit(fails ? 1 : 0);
