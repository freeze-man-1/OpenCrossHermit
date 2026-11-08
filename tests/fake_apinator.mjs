#!/usr/bin/env node
// ============================================================================
// fake_apinator.mjs — a local stand-in for an Apinator cluster, for the
// net_apinator ctest (no internet, no quota): /app/<key>, the realtime:*
// system events, private / presence channels whose signatures it checks
// with the secret, presence snapshots and member events, and client events
// fanned out to the channel's other subscribers. Only what the game uses
// (see https://api.apinator.io/api/v1/specs/asyncapi/realtime.json).
//
//   node tests/fake_apinator.mjs --port 8081 --key app_test --secret s3cret
// ============================================================================
import http from 'node:http';
import crypto from 'node:crypto';

const args = process.argv.slice(2);
const opt = (name, dflt) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : dflt; };
const PORT = Number(opt('--port', 8081)), KEY = opt('--key', 'app_test'), SECRET = opt('--secret', 's3cret');

function frame(op, payload) {
  const n = payload.length;
  const head = n < 126 ? Buffer.from([0x80 | op, n])
             : n < 65536 ? Buffer.from([0x80 | op, 126, n >> 8, n & 0xff])
             : (() => { const h = Buffer.alloc(10); h[0] = 0x80 | op; h[1] = 127; h.writeBigUInt64BE(BigInt(n), 2); return h; })();
  return Buffer.concat([head, payload]);
}

const channels = new Map();   // name -> Map(conn -> member or null)
let nextId = 1;

class Conn {
  constructor(socket) {
    this.socket = socket; this.buf = Buffer.alloc(0); this.parts = []; this.closed = false;
    this.id = `${nextId++}.${crypto.randomInt(1e9)}`;
    this.subs = new Set();
    socket.on('data', d => this.feed(d));
    socket.on('close', () => this.close());
    socket.on('error', () => this.close());
    this.send('realtime:connection_established', null, { socket_id: this.id, activity_timeout: 120 });
  }
  send(event, channel, data) {
    if (this.closed) return;
    const m = { event, data: typeof data === 'string' ? data : JSON.stringify(data) };
    if (channel) m.channel = channel;
    this.socket.write(frame(1, Buffer.from(JSON.stringify(m))));
  }
  feed(d) {
    this.buf = Buffer.concat([this.buf, d]);
    for (;;) {
      if (this.buf.length < 2) return;
      const fin = this.buf[0] & 0x80, op = this.buf[0] & 0x0f;
      let n = this.buf[1] & 0x7f, at = 2;
      if (n === 126) { if (this.buf.length < 4) return; n = this.buf.readUInt16BE(2); at = 4; }
      else if (n === 127) { if (this.buf.length < 10) return; n = Number(this.buf.readBigUInt64BE(2)); at = 10; }
      if (this.buf.length < at + 4 + n) return;
      const mask = this.buf.subarray(at, at + 4);
      const data = Buffer.from(this.buf.subarray(at + 4, at + 4 + n));
      for (let i = 0; i < n; ++i) data[i] ^= mask[i & 3];
      this.buf = this.buf.subarray(at + 4 + n);
      if (op === 8) { this.close(); return; }
      if (op === 9) { this.socket.write(frame(10, data)); continue; }
      if (op === 10) continue;
      if (op === 2) { this.close(); return; }   // text only
      this.parts.push(data);
      if (fin) { const m = Buffer.concat(this.parts).toString('utf8'); this.parts = []; this.message(m); }
    }
  }
  message(text) {
    let m, d;
    try { m = JSON.parse(text); d = typeof m.data === 'string' ? JSON.parse(m.data || '{}') : (m.data ?? {}); } catch { return; }
    if (m.event === 'realtime:ping') { this.send('realtime:pong', null, ''); return; }
    if (m.event === 'realtime:subscribe') { this.subscribe(d); return; }
    if (m.event === 'realtime:unsubscribe') { this.leave(d.channel); return; }
    if (typeof m.event === 'string' && m.event.startsWith('client-')) {
      const ch = channels.get(m.channel);
      if (!ch || !ch.has(this) || !/^(private|presence)-/.test(m.channel)) {
        this.send('realtime:error', null, { code: 4301, message: 'client events need a private channel you joined' });
        return;
      }
      for (const o of ch.keys()) if (o !== this) o.send(m.event, m.channel, m.data);
    }
  }
  subscribe(d) {
    const name = String(d.channel ?? '');
    const refuse = why => this.send('realtime:subscription_error', name, { type: 'AuthError', error: why, status: 401 });
    let member = null;
    if (/^(private|presence)-/.test(name)) {
      let toSign = `${this.id}:${name}`;
      if (name.startsWith('presence-')) {
        try { member = JSON.parse(d.channel_data); } catch { return refuse('bad channel_data'); }
        toSign += `:${d.channel_data}`;
      }
      const want = `${KEY}:${crypto.createHmac('sha256', SECRET).update(toSign).digest('hex')}`;
      if (d.auth !== want) return refuse('invalid signature');
    }
    if (!channels.has(name)) channels.set(name, new Map());
    const ch = channels.get(name);
    if (ch.has(this)) return;
    ch.set(this, member);
    this.subs.add(name);
    if (member) {
      const hash = {};
      for (const v of ch.values()) if (v) hash[v.user_id] = v.user_info;
      this.send('realtime:subscription_succeeded', name, { presence: { count: Object.keys(hash).length, ids: Object.keys(hash), hash } });
      for (const o of ch.keys()) if (o !== this) o.send('realtime:member_added', name, member);
    } else {
      this.send('realtime:subscription_succeeded', name, {});
    }
  }
  leave(name) {
    const ch = channels.get(name);
    if (!ch || !ch.has(this)) return;
    const member = ch.get(this);
    ch.delete(this);
    this.subs.delete(name);
    if (member) for (const o of ch.keys()) o.send('realtime:member_removed', name, { user_id: member.user_id });
    if (ch.size === 0) channels.delete(name);
  }
  close() {
    if (this.closed) return;
    this.closed = true;
    for (const name of [...this.subs]) this.leave(name);
    this.socket.destroy();
  }
}

const server = http.createServer((req, res) => { res.writeHead(200); res.end('fake apinator\n'); });
server.on('upgrade', (req, socket) => {
  const key = req.headers['sec-websocket-key'];
  if (!key || req.url !== `/app/${KEY}`) {
    socket.end('HTTP/1.1 401 Unauthorized\r\n\r\n');
    return;
  }
  const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
  socket.setNoDelay(true);
  new Conn(socket);
});
server.listen(PORT, () => console.log(`fake apinator on port ${PORT}`));
