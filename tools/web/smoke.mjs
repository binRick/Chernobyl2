// Web build smoke test: open the build in headless Chrome over the DevTools protocol, wait for the
// game to report its first frames, let it run, then save a screenshot and print what the page logged.
//   node tools/web/smoke.mjs [url] [seconds-to-run] [out.png]      (SMOKE_LOOK=1: also test mouse look)
// Needs Google Chrome and Node >= 22 (built-in WebSocket). Serve build/web/dist first (make web-serve).
import { spawn } from 'node:child_process';
import { writeFileSync, mkdirSync } from 'node:fs';
const url = process.argv[2] || 'http://127.0.0.1:8642/index.html?autostart=1';
const runSecs = parseFloat(process.argv[3] || '8');
const out = process.argv[4] || 'build/web/shot.png';
const port = 9333, prof = process.cwd() + '/build/web/chrome';
mkdirSync(prof, { recursive: true });
const chrome = spawn('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', ['--headless=new', '--no-first-run',
  '--disable-extensions', `--user-data-dir=${prof}`, '--window-size=1280,720', `--remote-debugging-port=${port}`, '--mute-audio',
  '--autoplay-policy=no-user-gesture-required', '--enable-unsafe-swiftshader', 'about:blank'], { stdio: 'ignore' });
const sleep = ms => new Promise(r => setTimeout(r, ms));
let ws, id = 0; const pending = new Map(), logs = [];
const send = (method, params = {}) => new Promise(res => { const i = ++id; pending.set(i, res); ws.send(JSON.stringify({ id: i, method, params })); });
try {
  let target;
  for (let k = 0; k < 50 && !target; k++) { await sleep(200); try { target = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find(t => t.type === 'page'); } catch {} }
  if (!target) throw new Error('chrome did not start');
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise(r => ws.addEventListener('open', r));
  ws.addEventListener('message', ev => { const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m.result || m.error); pending.delete(m.id); }
    else if (m.method === 'Runtime.consoleAPICalled') logs.push(`[${m.params.type}] ` + m.params.args.map(a => a.value ?? a.description ?? '').join(' '));
    else if (m.method === 'Runtime.exceptionThrown') logs.push('[exception] ' + (m.params.exceptionDetails.exception?.description || m.params.exceptionDetails.text));
    else if (m.method === 'Log.entryAdded') logs.push(`[${m.params.entry.level}] ${m.params.entry.text}`); });
  await send('Runtime.enable'); await send('Log.enable'); await send('Page.enable');
  await send('Emulation.setFocusEmulationEnabled', { enabled: true }); await send('Page.bringToFront');
  await send('Page.navigate', { url });
  const ev = async e => (await send('Runtime.evaluate', { expression: e, returnByValue: true })).result?.value;
  let ready = false; const t0 = Date.now();
  while (Date.now() - t0 < 120000 && !ready) { await sleep(500); ready = await ev("!!window.__c2_loaded"); }
  console.log(ready ? `ready after ${((Date.now() - t0) / 1000).toFixed(1)} s` : 'NOT READY after 120 s; status: ' + await ev("document.getElementById('cta').textContent"));
  for (let k = 0; k < +(process.env.SMOKE_SHOTS || 0); k++) {     // a strip of stills, runSecs apart: out_0.png, out_1.png ...
    await sleep(runSecs * 1000); const s = await send('Page.captureScreenshot', { format: 'png' });
    if (s.data) writeFileSync(out.replace(/\.png$/, `_${k}.png`), Buffer.from(s.data, 'base64'));
  }
  if (!process.env.SMOKE_SHOTS) await sleep(runSecs * 1000);
  if (process.env.SMOKE_INTRO) {           // the title intro: begin, a still of the montage, skip to the title, a still, start, a still of the game
    const shotTo = async tag => { const s = await send('Page.captureScreenshot', { format: 'png' }); writeFileSync(out.replace(/\.png$/, `_${tag}.png`), Buffer.from(s.data, 'base64')); };
    const key = async () => { await send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 }); await sleep(120); await send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 }); };
    const st = async () => 'video t=' + (await ev("document.getElementById('ivid').currentTime.toFixed(2)")) + ' music t=' + (await ev("(typeof music!=='undefined'&&music)?music.currentTime.toFixed(2):'-'")) + ' stage=' + (await ev("typeof stage!=='undefined'?stage:-1")) + ' go=' + (await ev('!!window.__c2_go'));
    await send('Input.dispatchMouseEvent', { type: 'mousePressed', x: 640, y: 360, button: 'left', clickCount: 1 }); await send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: 640, y: 360, button: 'left', clickCount: 1 });
    await sleep(6000); console.log('montage: ' + await st()); await shotTo('intro0');
    await key(); await sleep(2500); console.log('title:   ' + await st()); await shotTo('intro1');
    await key(); await sleep(3500); console.log('started: ' + await st() + ' locked=' + await ev('!!document.pointerLockElement')); await shotTo('intro2');
  }
  if (process.env.SMOKE_LOOK) {            // click to take pointer lock, then drag the view: before / after stills + whether the lock held
    const mouse = (type, x, y, extra = {}) => send('Input.dispatchMouseEvent', { type, x, y, button: 'left', clickCount: 1, ...extra });
    for (let k = 0; k < 2 && !(await ev('!!document.pointerLockElement')); k++) { await mouse('mousePressed', 640, 360); await mouse('mouseReleased', 640, 360); await sleep(600); }   // the gate's click, then the game's own re-grab
    console.log('pointer locked: ' + await ev('!!document.pointerLockElement') + '  gate: ' + await ev("document.getElementById('gate').style.display"));
    let s = await send('Page.captureScreenshot', { format: 'png' }); writeFileSync(out.replace(/\.png$/, '_look0.png'), Buffer.from(s.data, 'base64'));
    if (!(await ev('!!document.pointerLockElement'))) {        // headless Chrome refuses pointer lock: stand in for it, and feed the page relative moves
      console.log('pointer lock refused here: simulating it');
      await ev("Object.defineProperty(document,'pointerLockElement',{get:function(){return document.getElementById('canvas');},configurable:true}); document.dispatchEvent(new Event('pointerlockchange')); 1");
      for (let k = 1; k <= 30; k++) { await ev("document.dispatchEvent(new MouseEvent('mousemove',{movementX:12,movementY:0,bubbles:true})); 1"); await sleep(16); }
    } else
      for (let k = 1; k <= 30; k++) { await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: 640 + k * 12, y: 360, button: 'none' }); await sleep(16); }
    await sleep(300);
    s = await send('Page.captureScreenshot', { format: 'png' }); writeFileSync(out.replace(/\.png$/, '_look1.png'), Buffer.from(s.data, 'base64'));
  }
  if (process.env.SMOKE_TYPE) {            // type into the page (the death screen's initials), then ENTER
    for (const ch of process.env.SMOKE_TYPE) { await send('Input.dispatchKeyEvent', { type: 'keyDown', text: ch, key: ch, code: 'Key' + ch.toUpperCase(), windowsVirtualKeyCode: ch.toUpperCase().charCodeAt(0) });
      await send('Input.dispatchKeyEvent', { type: 'keyUp', key: ch, code: 'Key' + ch.toUpperCase(), windowsVirtualKeyCode: ch.toUpperCase().charCodeAt(0) }); await sleep(120); }
    await send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 }); await sleep(150);   // held across a frame: the game polls keys per frame
    await send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 });
    await sleep(3000);
  }
  const shot = await send('Page.captureScreenshot', { format: 'png' });
  if (shot.data) { writeFileSync(out, Buffer.from(shot.data, 'base64')); console.log('screenshot ' + out); }
  console.log('heap MB: ' + await ev("(typeof HEAPU8!=='undefined')?Math.round(HEAPU8.length/1048576):-1"));
  const keep = logs.filter(l => !/^\[(log|info)\] (INFO|DEBUG): (FILEIO|IMAGE|TEXTURE|MODEL|VAO|SHADER: \[ID \d+\] (Vertex|Fragment) shader compiled|TIMER|AUDIO|WAVE|STREAM|FONT|GL: |DISPLAY|PLATFORM|RLGL|SYSTEM|FBO)/.test(l));
  const tally = new Map(); for (const l of keep) tally.set(l, (tally.get(l) || 0) + 1);       // each distinct line once, with its count
  console.log(`console: ${logs.length} lines, ${tally.size} distinct shown`);
  for (const [l, n] of [...tally].slice(0, 90)) console.log(`  ${n > 1 ? n + 'x ' : ''}${l.slice(0, 260)}`);
} catch (e) { console.log('smoke failed: ' + e.message); }
finally { try { ws && ws.close(); } catch {} chrome.kill(); }
