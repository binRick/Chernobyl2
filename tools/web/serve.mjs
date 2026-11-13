// Dev server for the web build: serves build/web/dist and, like the live site's nginx, hands
// /api/* to a score server (run chernobyl2-scores/server.js on SCORES_PORT, default 3912).
//   node tools/web/serve.mjs [port]
import http from 'node:http'; import { readFile } from 'node:fs/promises'; import { extname, join, normalize } from 'node:path';
const port = +(process.argv[2] || 8642), api = +(process.env.SCORES_PORT || 3912), root = join(process.cwd(), 'build/web/dist');
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream', '.png': 'image/png',
  '.jpg': 'image/jpeg', '.mp4': 'video/mp4', '.mp3': 'audio/mpeg', '.txt': 'text/plain' };
http.createServer(async (req, res) => {
  if (req.url.startsWith('/api/')) {
    const p = http.request({ host: '127.0.0.1', port: api, path: req.url, method: req.method, headers: req.headers }, r => { res.writeHead(r.statusCode, r.headers); r.pipe(res); });
    p.on('error', () => { res.writeHead(502); res.end('{"error":"no score server"}'); }); req.pipe(p); return;
  }
  const f = normalize(join(root, decodeURIComponent(req.url.split('?')[0]) === '/' ? 'index.html' : decodeURIComponent(req.url.split('?')[0])));
  if (!f.startsWith(root)) { res.writeHead(403); return res.end(); }
  try { const d = await readFile(f), type = types[extname(f)] || 'application/octet-stream';
    const r = /^bytes=(\d*)-(\d*)$/.exec(req.headers.range || '');          // byte ranges: a browser needs them to seek in the intro's video and music
    if (r && (r[1] || r[2])) { const a = r[1] ? +r[1] : d.length - +r[2], b = r[1] && r[2] ? Math.min(+r[2], d.length - 1) : d.length - 1;
      res.writeHead(206, { 'Content-Type': type, 'Accept-Ranges': 'bytes', 'Content-Range': `bytes ${a}-${b}/${d.length}`, 'Content-Length': b - a + 1 }); return res.end(d.subarray(a, b + 1)); }
    res.writeHead(200, { 'Content-Type': type, 'Accept-Ranges': 'bytes', 'Content-Length': d.length }); res.end(d); }
  catch { res.writeHead(404); res.end('not found'); }
}).listen(port, '127.0.0.1', () => console.log(`serving ${root} on http://127.0.0.1:${port}/ (api -> :${api})`));
