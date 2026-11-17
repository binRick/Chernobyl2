// Web intro mute check: M on the page's title intro mutes it as M does the game -- never a skip -- the MUTE label (top
// right) does the same on a click or tap, and the game starts the way the intro was left (window.__c2_muted, WebFrame).
// Drives a headless Chrome (sound off: --mute-audio) over the DevTools protocol; keys and clicks go to that page only.
//   node tools/web/intro-mute.mjs [base-url]        (serve build/web/dist first: node tools/web/serve.mjs)
import { spawn } from 'node:child_process';
import { mkdirSync } from 'node:fs';
const base = process.argv[2] || 'http://127.0.0.1:8642/';
const port = +(process.env.SMOKE_PORT || 9334), prof = process.cwd() + '/build/web/chrome-intro';
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
    else if (m.method === 'Runtime.consoleAPICalled') logs.push(m.params.args.map(a => a.value ?? a.description ?? '').join(' '));
    else if (m.method === 'Runtime.exceptionThrown') logs.push('[exception] ' + (m.params.exceptionDetails.exception?.description || m.params.exceptionDetails.text)); });
  await send('Runtime.enable'); await send('Page.enable');
  await send('Emulation.setFocusEmulationEnabled', { enabled: true }); await send('Page.bringToFront');
  await send('Page.navigate', { url: base + 'index.html?args=--debug' });
  const ev = async e => (await send('Runtime.evaluate', { expression: e, returnByValue: true })).result?.value;
  const key = async (k, code, vk) => { for (const type of ['keyDown', 'keyUp']) await send('Input.dispatchKeyEvent', { type, key: k, code, windowsVirtualKeyCode: vk, text: type === 'keyDown' ? k : undefined }); await sleep(150); };
  const click = async sel => { const r = await ev(`(function(){ var b=document.querySelector('${sel}').getBoundingClientRect(); return [b.left+b.width/2, b.top+b.height/2]; })()`);
    for (const type of ['mousePressed', 'mouseReleased']) await send('Input.dispatchMouseEvent', { type, x: r[0], y: r[1], button: 'left', clickCount: 1 }); await sleep(150); };
  const state = () => ev(`JSON.stringify({ stage: stage, muted: !!window.__c2_muted, label: document.getElementById('imute').textContent, music: music ? music.muted : null })`).then(JSON.parse);
  let t0 = Date.now(); while (Date.now() - t0 < 30000 && await ev('typeof stage!=="undefined" && stage') !== 1) await sleep(200);
  ok(await ev('stage') === 1, 'the montage is playing');
  await sleep(500);
  await key('m', 'KeyM', 77); let s = await state();
  ok(s.stage === 1 && s.muted && /MUTED/.test(s.label) && s.music === true, `M mutes it, the montage plays on: ${JSON.stringify(s)}`);
  await key('m', 'KeyM', 77); s = await state();
  ok(s.stage === 1 && !s.muted && /MUTE/.test(s.label) && !/MUTED/.test(s.label) && s.music === false, `M again: sound back: ${JSON.stringify(s)}`);
  await click('#imute'); s = await state();
  ok(s.stage === 1 && s.muted && s.music === true, `a click on MUTE mutes it and is not a skip: ${JSON.stringify(s)}`);
  await sleep(1600); await key('x', 'KeyX', 88); s = await state();
  ok(s.stage === 2 && s.muted, `any other key still skips to the title, muted: ${JSON.stringify(s)}`);
  t0 = Date.now(); while (Date.now() - t0 < 120000 && !await ev('!!window.__c2_loaded')) await sleep(500);
  await sleep(1500); await key('x', 'KeyX', 88);                       // start
  await sleep(4000);
  const dbg = await ev(`(function(){ try { return FS.readFile('chernobyl2-debug.log', { encoding: 'utf8' }); } catch (e) { return 'ERR ' + e; } })()`) || '';
  ok(await ev('!!window.__c2_go'), 'the game started');
  ok(/"ev":"mute","on":1,"intro":1/.test(dbg), 'and started muted: ' + ((dbg.match(/\{[^\n]*"ev":"mute"[^\n]*\}/) || [dbg.slice(0, 120)])[0]));
  const ex = logs.filter(l => l.startsWith('[exception]')); ok(ex.length === 0, 'no page exceptions' + (ex.length ? ': ' + ex[0] : ''));
} catch (e) { console.log('FAIL ' + e.message); fails++; }
finally { try { chrome.kill(); } catch {} }
console.log(fails ? 'FAIL' : 'PASS'); process.exit(fails ? 1 : 0);
