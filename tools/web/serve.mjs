// Dev server for the web build: serves build/web/dist and, like the live site's nginx, hands
// /api/* to a score server (run chernobyl2-scores/server.js on SCORES_PORT, default 3912).
//   node tools/web/serve.mjs [port]
import http from 'node:http'; import { readFile } from 'node:fs/promises'; import { extname, join, normalize } from 'node:path';
const port = +(process.argv[2] || 8642), api = +(process.env.SCORES_PORT || 3912), root = join(process.cwd(), 'build/web/dist');
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream', '.png': 'image/png' };
http.createServer(async (req, res) => {
  if (req.url.startsWith('/api/')) {
    const p = http.request({ host: '127.0.0.1', port: api, path: req.url, method: req.method, headers: req.headers }, r => { res.writeHead(r.statusCode, r.headers); r.pipe(res); });
    p.on('error', () => { res.writeHead(502); res.end('{"error":"no score server"}'); }); req.pipe(p); return;
  }
  const f = normalize(join(root, decodeURIComponent(req.url.split('?')[0]) === '/' ? 'index.html' : decodeURIComponent(req.url.split('?')[0])));
  if (!f.startsWith(root)) { res.writeHead(403); return res.end(); }
  try { const d = await readFile(f); res.writeHead(200, { 'Content-Type': types[extname(f)] || 'application/octet-stream' }); res.end(d); }
  catch { res.writeHead(404); res.end('not found'); }
}).listen(port, '127.0.0.1', () => console.log(`serving ${root} on http://127.0.0.1:${port}/ (api -> :${api})`));
