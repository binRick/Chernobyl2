// mp-relay.js -- the multiplayer relay for Chernobyl 2 (src/mp.h is the game's side).
//
// Players cannot reach each other directly (a browser only speaks WebSockets, and nobody's home
// router lets a stranger in), so every game meets here: a room of up to eight sockets, the first of
// which is the HOST -- his game runs the enemies and the waves. The relay knows nothing of the game:
// it keeps the rooms, hands out player numbers, and passes each message on to the whole room, to the
// host, or to one player, with the sender's number put in front. When the host goes, the room goes.
//
// No dependencies (the WebSocket handshake and framing are below), so it runs wherever node does:
//   node server/mp-relay.js [port]          standalone, for development (default 8643)
//   require('./mp-relay').attach(server)    on an existing http server, at the path /mp
//
// Every frame is binary; the first byte says what it is.
//   client -> relay                            relay -> client
//   0x01 CREATE name[3]                        0x81 WELCOME you, host, code[4], n, n x (id, name[3])
//   0x02 JOIN   code[4] name[3]                0x82 LIST    n, n x (code[4], name[3], players, wave)
//   0x03 LIST                                  0x83 JOINED  id, name[3]
//   0x04 ALL    payload   (everyone else)      0x84 LEFT    id
//   0x05 HOST   payload   (the host only)      0x85 MSG     from, payload
//   0x06 ONE    id payload                     0x86 ERROR   why (1 no such game, 2 full, 3 bad request)
//   0x07 INFO   wave      (host: for LIST)     0x87 CLOSED  (the host left)
//   0x08 LEAVE
'use strict';
const crypto = require('crypto');
const http = require('http');

const MAX_PLAYERS = 8, MAX_ROOMS = 200, MAX_FRAME = 16384;
const rooms = new Map();                       // code -> { code, host, wave, peers: Map(id -> conn) }

function frame(buf) {                          // one unmasked binary frame
  const n = buf.length;
  let h;
  if (n < 126) h = Buffer.from([0x82, n]);
  else if (n < 65536) { h = Buffer.alloc(4); h[0] = 0x82; h[1] = 126; h.writeUInt16BE(n, 2); }
  else { h = Buffer.alloc(10); h[0] = 0x82; h[1] = 127; h.writeUInt32BE(0, 2); h.writeUInt32BE(n, 6); }
  return Buffer.concat([h, buf]);
}
function send(c, buf) { if (c && !c.dead) { try { c.sock.write(frame(buf)); } catch (e) { /* gone */ } } }
function name3(b, at) {                        // three printable characters, whatever was sent
  let s = '';
  for (let i = 0; i < 3; i++) { const ch = b[at + i]; s += (ch >= 48 && ch <= 90) ? String.fromCharCode(ch) : '_'; }
  return s;
}
function newCode() {
  const A = 'ABCDEFGHJKLMNPQRSTUVWXYZ';
  for (;;) { let s = ''; for (let i = 0; i < 4; i++) s += A[crypto.randomInt(A.length)]; if (!rooms.has(s)) return s; }
}
function welcome(c) {
  const r = c.room, parts = [Buffer.from([0x81, c.id, r.host]), Buffer.from(r.code, 'latin1'), Buffer.from([r.peers.size - 1])];
  for (const [id, p] of r.peers) if (id !== c.id) parts.push(Buffer.from([id]), Buffer.from(p.name, 'latin1'));
  send(c, Buffer.concat(parts));
}
function leave(c) {
  const r = c.room; if (!r) return;
  c.room = null; r.peers.delete(c.id);
  if (c.id === r.host || r.peers.size === 0) {  // the host's game was the game
    for (const p of r.peers.values()) { p.room = null; send(p, Buffer.from([0x87])); }
    rooms.delete(r.code);
  } else for (const p of r.peers.values()) send(p, Buffer.from([0x84, c.id]));
}
function onMessage(c, m) {
  if (m.length < 1) return;
  const t = m[0], r = c.room;
  if (t === 0x01 && m.length >= 4) {            // CREATE
    if (r) leave(c);
    if (rooms.size >= MAX_ROOMS) return send(c, Buffer.from([0x86, 2]));
    const room = { code: newCode(), host: 0, wave: 0, peers: new Map() };
    c.id = 0; c.name = name3(m, 1); c.room = room; room.peers.set(0, c); rooms.set(room.code, room);
    welcome(c);
  } else if (t === 0x02 && m.length >= 8) {     // JOIN
    if (r) leave(c);
    const room = rooms.get(m.toString('latin1', 1, 5));
    if (!room) return send(c, Buffer.from([0x86, 1]));
    if (room.peers.size >= MAX_PLAYERS) return send(c, Buffer.from([0x86, 2]));
    let id = 0; while (room.peers.has(id)) id++;
    c.id = id; c.name = name3(m, 5); c.room = room;
    for (const p of room.peers.values()) send(p, Buffer.concat([Buffer.from([0x83, id]), Buffer.from(c.name, 'latin1')]));
    room.peers.set(id, c);
    welcome(c);
  } else if (t === 0x03) {                      // LIST
    const parts = [Buffer.from([0x82, 0])]; let n = 0;
    for (const room of rooms.values()) {
      if (n >= 40) break;
      const h = room.peers.get(room.host); if (!h) continue;
      parts.push(Buffer.from(room.code, 'latin1'), Buffer.from(h.name, 'latin1'), Buffer.from([room.peers.size, room.wave & 255])); n++;
    }
    parts[0][1] = n; send(c, Buffer.concat(parts));
  } else if (t === 0x04 && r) {                 // ALL
    const out = Buffer.concat([Buffer.from([0x85, c.id]), m.subarray(1)]);
    for (const [id, p] of r.peers) if (id !== c.id) send(p, out);
  } else if (t === 0x05 && r) {                 // HOST
    if (c.id !== r.host) send(r.peers.get(r.host), Buffer.concat([Buffer.from([0x85, c.id]), m.subarray(1)]));
  } else if (t === 0x06 && r && m.length >= 2) {// ONE
    send(r.peers.get(m[1]), Buffer.concat([Buffer.from([0x85, c.id]), m.subarray(2)]));
  } else if (t === 0x07 && r && m.length >= 2) {// INFO
    if (c.id === r.host) r.wave = m[1];
  } else if (t === 0x08) leave(c);
  else if (!r && t >= 0x04 && t <= 0x07) { /* not in a game: nothing to pass on */ }
  else send(c, Buffer.from([0x86, 3]));
}

function onUpgrade(req, sock) {
  const key = req.headers['sec-websocket-key'];
  if (!key || (req.headers.upgrade || '').toLowerCase() !== 'websocket') { sock.destroy(); return; }
  const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  sock.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + '\r\n\r\n');
  sock.setNoDelay(true);
  const c = { sock, room: null, id: -1, name: '___', dead: false, buf: Buffer.alloc(0), part: null };
  const gone = () => { if (c.dead) return; c.dead = true; leave(c); sock.destroy(); };
  sock.on('data', (d) => {
    c.buf = c.buf.length ? Buffer.concat([c.buf, d]) : d;
    for (;;) {                                  // every whole frame in the buffer
      if (c.buf.length < 2) return;
      const fin = c.buf[0] & 0x80, op = c.buf[0] & 0x0f, masked = c.buf[1] & 0x80;
      let len = c.buf[1] & 0x7f, at = 2;
      if (len === 126) { if (c.buf.length < 4) return; len = c.buf.readUInt16BE(2); at = 4; }
      else if (len === 127) { if (c.buf.length < 10) return; if (c.buf.readUInt32BE(2)) return gone(); len = c.buf.readUInt32BE(6); at = 10; }
      if (len > MAX_FRAME || !masked) return gone();
      if (c.buf.length < at + 4 + len) return;
      const mask = c.buf.subarray(at, at + 4), data = Buffer.from(c.buf.subarray(at + 4, at + 4 + len));
      for (let i = 0; i < len; i++) data[i] ^= mask[i & 3];
      c.buf = c.buf.subarray(at + 4 + len);
      if (op === 8) return gone();
      if (op === 9) { try { sock.write(Buffer.concat([Buffer.from([0x8a, data.length]), data])); } catch (e) { /* gone */ } continue; }
      if (op === 10) continue;
      let whole = data;                         // a message can come in pieces
      if (op === 0) { if (!c.part) return gone(); c.part.push(data); if (!fin) continue; whole = Buffer.concat(c.part); c.part = null; }
      else if (!fin) { c.part = [data]; continue; }
      if (whole.length > MAX_FRAME) return gone();
      try { onMessage(c, whole); } catch (e) { console.error('mp-relay:', e.message); }
    }
  });
  sock.on('close', gone); sock.on('error', gone);
}

function attach(server, path) {
  path = path || '/mp';
  server.on('upgrade', (req, sock) => { if ((req.url || '').split('?')[0] === path) onUpgrade(req, sock); else sock.destroy(); });
  return { rooms };
}
module.exports = { attach, rooms };

if (require.main === module) {
  const port = parseInt(process.argv[2] || process.env.PORT || '8643', 10);
  const server = http.createServer((req, res) => {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ rooms: rooms.size, players: [...rooms.values()].reduce((a, r) => a + r.peers.size, 0) }));
  });
  attach(server);
  server.listen(port, () => console.log('mp-relay: listening on :' + port + ' (ws path /mp)'));
}
