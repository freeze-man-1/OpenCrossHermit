#!/usr/bin/env node
// ============================================================================
// relay.mjs — the Cross Hermit network relay: lobby, rooms and the lockstep
// turn clock. No dependencies (Node 18+): the WebSocket framing is done here.
//
//   node tools/netserver/relay.mjs [--port 8787] [--turn-frames 2] [--verbose]
//
// The protocol is described in game/net/net_protocol.hpp; keep both in step.
//
// What the relay does, besides forwarding:
//   - Owns the clock. Turns close on its timer (turnFrames / 60 s each), so a
//     slow or stalled player never freezes the others and no client can run
//     the battle faster than everyone else (no speed hacks).
//   - Input that misses its turn is moved to the next open one, never lost.
//   - Measures each player's round trip (Ping / Pong) and sets the room's
//     input delay from the slowest player: just enough turns for a command to
//     reach everyone before its turn closes.
//   - Stamps every command with its sender's slot (no one can send for
//     another player) and limits sizes and rates.
//   - Compares the players' CHECK reports (generator state, roll count, unit
//     hash) turn by turn. A mismatch is a desync (or a tampered client): with
//     three or more players the odd ones out are named, with two the match
//     is stopped for both. Commands every client refused (another player's
//     unit, an unknown order) are reported against their sender.
// ============================================================================
import http from 'node:http';
import crypto from 'node:crypto';

const args = process.argv.slice(2);
const opt = (name, dflt) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : dflt; };
const PORT = Number(opt('--port', process.env.PORT ?? 8787));
const DEFAULT_TURN_FRAMES = Number(opt('--turn-frames', 2));
const VERBOSE = args.includes('--verbose');
const log = (...a) => console.log(new Date().toISOString().slice(11, 23), ...a);
const debug = (...a) => { if (VERBOSE) log(...a); };

const PROTOCOL = 1;
const MAX_PLAYERS = 4;
const MAX_COMMANDS = 64;
const MAX_MESSAGE = 4096;
const CHECK_EVERY = 15;          // turns between CHECK reports (~0.5 s at 2 frames a turn)
const MSG = {
  Hello: 1, ListRooms: 2, CreateRoom: 3, JoinRoom: 4, LeaveRoom: 5, Chat: 6, SetReady: 7,
  SetOptions: 8, Start: 9, Input: 10, Check: 11, Ping: 12, SetPlayerData: 13, Loaded: 14, GameOver: 15,
  Welcome: 101, RoomList: 102, RoomState: 103, ChatFrom: 104, Started: 105, Turn: 106,
  Pong: 107, Error: 108, Desync: 109, PlayerLeft: 110, Stats: 111,
};

// ---------------------------------------------------------------- codec ---
class W {
  constructor(type) { this.b = [type]; }
  u8(v) { this.b.push(v & 0xff); return this; }
  u16(v) { return this.u8(v).u8(v >> 8); }
  u32(v) { return this.u16(v & 0xffff).u16((v >>> 16) & 0xffff); }
  // UTF-8, at most 255 bytes, never cutting a character in two.
  str(s) {
    let e = Buffer.from(String(s), 'utf8');
    if (e.length > 255) { let n = 255; while (n > 0 && (e[n] & 0xc0) === 0x80) --n; e = e.subarray(0, n); }
    this.u8(e.length); for (const c of e) this.b.push(c); return this;
  }
  bytes(buf) { this.u16(buf.length); for (const c of buf) this.b.push(c); return this; }
  raw(buf) { for (const c of buf) this.b.push(c); return this; }
  done() { return Buffer.from(this.b); }
}
class R {
  constructor(buf) { this.b = buf; this.at = 0; this.ok = true; }
  need(n) { if (this.at + n > this.b.length) { this.ok = false; return false; } return true; }
  u8() { if (!this.need(1)) return 0; return this.b[this.at++]; }
  u16() { if (!this.need(2)) return 0; const v = this.b.readUInt16LE(this.at); this.at += 2; return v; }
  u32() { if (!this.need(4)) return 0; const v = this.b.readUInt32LE(this.at); this.at += 4; return v; }
  str() { const n = this.u8(); if (!this.need(n)) return ''; const s = this.b.toString('utf8', this.at, this.at + n); this.at += n; return s; }
  bytes() { const n = this.u16(); if (!this.need(n)) return Buffer.alloc(0); const v = this.b.subarray(this.at, this.at + n); this.at += n; return Buffer.from(v); }
  raw(n) { if (!this.need(n)) return Buffer.alloc(0); const v = this.b.subarray(this.at, this.at + n); this.at += n; return Buffer.from(v); }
}

// ------------------------------------------------------------ websocket ---
// RFC 6455, server side: unmasked frames out, masked frames in; binary
// messages only, fragmented messages reassembled, ping answered.
function wsAccept(req, socket) {
  const key = req.headers['sec-websocket-key'];
  if (!key || (req.headers.upgrade || '').toLowerCase() !== 'websocket') { socket.destroy(); return false; }
  const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
               `Sec-WebSocket-Accept: ${accept}\r\n\r\n`);
  socket.setNoDelay(true);       // small frames go out at once (no Nagle delay)
  return true;
}
function wsFrame(opcode, payload) {
  const n = payload.length;
  const head = n < 126 ? Buffer.from([0x80 | opcode, n])
             : n < 65536 ? Buffer.from([0x80 | opcode, 126, n >> 8, n & 0xff])
             : (() => { const h = Buffer.alloc(10); h[0] = 0x80 | opcode; h[1] = 127; h.writeBigUInt64BE(BigInt(n), 2); return h; })();
  return Buffer.concat([head, payload]);
}
class Conn {
  constructor(socket, onMessage, onClose) {
    this.socket = socket; this.buf = Buffer.alloc(0); this.parts = []; this.closed = false;
    this.onMessage = onMessage; this.onClose = onClose;
    socket.on('data', d => this.feed(d));
    socket.on('close', () => this.close());
    socket.on('error', () => this.close());
  }
  feed(d) {
    this.buf = Buffer.concat([this.buf, d]);
    for (;;) {
      if (this.buf.length < 2) return;
      const fin = this.buf[0] & 0x80, op = this.buf[0] & 0x0f, masked = this.buf[1] & 0x80;
      let n = this.buf[1] & 0x7f, at = 2;
      if (n === 126) { if (this.buf.length < 4) return; n = this.buf.readUInt16BE(2); at = 4; }
      else if (n === 127) { if (this.buf.length < 10) return; n = Number(this.buf.readBigUInt64BE(2)); at = 10; }
      if (!masked || n > MAX_MESSAGE) { this.close(); return; }
      if (this.buf.length < at + 4 + n) return;
      const mask = this.buf.subarray(at, at + 4);
      const data = Buffer.from(this.buf.subarray(at + 4, at + 4 + n));
      for (let i = 0; i < n; ++i) data[i] ^= mask[i & 3];
      this.buf = this.buf.subarray(at + 4 + n);
      if (op === 8) { this.close(); return; }
      if (op === 9) { this.socket.write(wsFrame(10, data)); continue; }
      if (op === 10) continue;
      this.parts.push(data);
      if (fin) { const m = Buffer.concat(this.parts); this.parts = []; this.onMessage(m); }
    }
  }
  send(buf) { if (!this.closed) this.socket.write(wsFrame(2, buf)); }
  close() {
    if (this.closed) return;
    this.closed = true;
    try { this.socket.end(wsFrame(8, Buffer.alloc(0))); } catch { /* gone */ }
    this.socket.destroy();
    this.onClose();
  }
}

// ---------------------------------------------------------------- rooms ---
const rooms = new Map();           // code -> Room
let nextPlayerId = 1;
const CODE_CHARS = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';   // no 0/O, 1/I
function newCode() {
  for (;;) {
    let c = '';
    for (let i = 0; i < 6; ++i) c += CODE_CHARS[crypto.randomInt(CODE_CHARS.length)];
    if (!rooms.has(c)) return c;
  }
}

class Player {
  constructor(conn) {
    this.conn = conn; this.id = nextPlayerId++; this.name = ''; this.build = 0; this.hello = false;
    this.room = null; this.slot = -1; this.ready = false; this.data = Buffer.alloc(0);
    this.rtt = 100; this.jitter = 20;   // until measured
    this.rate = { start: Date.now(), inputs: 0, chats: 0 };
    this.late = 0;
  }
  send(buf) { this.conn.send(buf); }
  error(code, text) { this.send(new W(MSG.Error).u8(code).str(text).done()); }
}

class Room {
  constructor(host, name, max, isPublic) {
    this.code = newCode(); this.name = name.slice(0, 40) || `${host.name}'s room`;
    this.max = Math.min(Math.max(max, 2), MAX_PLAYERS); this.isPublic = isPublic;
    this.slots = new Array(this.max).fill(null); this.host = 0; this.build = host.build;
    this.options = Buffer.alloc(0); this.inGame = false;
  }
  players() { return this.slots.filter(Boolean); }
  add(p) {
    const s = this.slots.indexOf(null);
    if (s < 0) return false;
    this.slots[s] = p; p.room = this; p.slot = s; p.ready = false; p.data = Buffer.alloc(0);
    return true;
  }
  remove(p) {
    if (p.room !== this) return;
    this.slots[p.slot] = null;
    const slot = p.slot;
    p.room = null; p.slot = -1;
    if (this.players().length === 0) { this.stop(); rooms.delete(this.code); log(`room ${this.code} closed`); return; }
    if (this.host === slot) this.host = this.slots.findIndex(Boolean);
    if (this.inGame) {
      // Into the next turn, for every client at the same frame: command
      // 0x43 "this slot has gone" (stamped with the slot that left).
      const gone = Buffer.alloc(16);
      gone[0] = 0x43;
      gone.writeInt16LE(-1, 2);
      if (!this.pending.has(this.turn)) this.pending.set(this.turn, new Map());
      const per = this.pending.get(this.turn);
      per.set(slot, (per.get(slot) ?? []).concat([gone]));
      for (const o of this.players()) o.send(new W(MSG.PlayerLeft).u8(slot).done());
      if (this.players().length < 2) this.stop();
    }
    this.broadcastState();
  }
  stateFor(p) {
    const w = new W(MSG.RoomState).str(this.code).str(this.name).u8(this.host).u8(p.slot).bytes(this.options);
    const list = this.players();
    w.u8(list.length);
    for (const o of list) w.u8(o.slot).str(o.name).u8(o.ready ? 1 : 0).u16(Math.min(Math.round(o.rtt), 65535)).bytes(o.data);
    return w.done();
  }
  broadcastState() { for (const p of this.players()) p.send(this.stateFor(p)); }
  broadcast(buf) { for (const p of this.players()) p.send(buf); }

  // ------------------------------------------------------------ the game ---
  start(turnFrames) {
    this.inGame = true;
    this.over = null;
    this.turnFrames = turnFrames;
    this.turnMs = turnFrames * 1000 / 60;
    this.turn = 0;                   // the next turn to close
    this.pending = new Map();        // turn -> Map(slot -> [commands Buffer])
    this.checks = new Map();         // turn -> Map(slot -> key string)
    this.delay = this.computeDelay();
    this.seed = crypto.randomInt(0x7fffffff);
    const list = this.players();
    for (const p of list) {
      p.late = 0;
      p.send(new W(MSG.Started).u32(this.seed).u8(turnFrames).u8(this.delay).u8(CHECK_EVERY).u8(p.slot).u8(this.max).done());
    }
    log(`room ${this.code}: started, ${list.length} players, turn ${this.turnMs.toFixed(1)} ms, delay ${this.delay}`);
    // The clock waits until every player's battle is loaded (Loaded), or a
    // minute at most, so no one starts a long way behind.
    this.loaded = new Set();
    this.loadTimer = setTimeout(() => this.run(), 60000);
  }
  markLoaded(p) {
    if (!this.inGame || this.timer) return;
    this.loaded.add(p.slot);
    if (this.players().every(o => this.loaded.has(o.slot))) this.run();
  }
  run() {
    if (!this.inGame || this.timer) return;
    clearTimeout(this.loadTimer);
    this.t0 = performance.now();
    this.schedule();
  }
  stop() {
    if (this.timer) clearTimeout(this.timer);
    if (this.loadTimer) clearTimeout(this.loadTimer);
    this.timer = null;
    this.inGame = false;
    for (const p of this.players()) p.ready = false;
  }
  // Turns close on an absolute schedule, so timer drift does not add up.
  schedule() {
    const due = this.t0 + (this.turn + 1) * this.turnMs;
    this.timer = setTimeout(() => this.closeTurn(), Math.max(0, due - performance.now()));
  }
  // Enough turns for a command to reach the relay and come back to everyone
  // before its turn closes: the slowest round trip plus twice its jitter and
  // a small margin, at least 2 turns.
  computeDelay() {
    let worst = 0;
    for (const p of this.players()) worst = Math.max(worst, p.rtt + 2 * p.jitter);
    return Math.min(Math.max(Math.ceil((worst + 8) / (this.turnMs || 33.3)) + 1, 2), 20);
  }
  addInput(p, turn, cmds) {
    // Late input goes to the next turn still open; input far ahead is refused.
    if (turn < this.turn) { turn = this.turn; ++p.late; }
    if (turn > this.turn + 60) return;
    if (!this.pending.has(turn)) this.pending.set(turn, new Map());
    const per = this.pending.get(turn);
    const have = per.get(p.slot) ?? [];
    if (have.length + cmds.length > MAX_COMMANDS) { p.error(3, 'too many commands'); return; }
    per.set(p.slot, have.concat(cmds));
  }
  closeTurn() {
    if (!this.inGame) return;
    const t = this.turn++;
    const per = this.pending.get(t) ?? new Map();
    this.pending.delete(t);
    // Every 30 turns the delay follows the room's round trips: up at once,
    // down one turn at a time (a sudden drop would leave gaps).
    if (t % 30 === 29) {
      const want = this.computeDelay();
      if (want > this.delay) this.delay = want;
      else if (want < this.delay) --this.delay;
    }
    const w = new W(MSG.Turn).u32(t).u8(this.delay);
    const entries = [...per.entries()].filter(([, c]) => c.length);
    w.u8(entries.length);
    for (const [slot, cmds] of entries) { w.u8(slot).u8(cmds.length); for (const c of cmds) w.raw(c); }
    this.broadcast(w.done());
    // Reports a stalled player never completed.
    for (const k of this.checks.keys()) if (k < t - 300) this.checks.delete(k);
    this.schedule();
  }
  addCheck(p, turn, key, refusedMask) {
    if (!this.checks.has(turn)) this.checks.set(turn, new Map());
    const m = this.checks.get(turn);
    m.set(p.slot, { key, refusedMask });
    const list = this.players();
    if (m.size < list.length) return;
    this.checks.delete(turn);
    // Everyone refused the same sender's commands: an illegal order (a
    // client sending for units it does not own, or made-up orders).
    let refusedBy = 0xff;
    for (const v of m.values()) refusedBy &= v.refusedMask;
    for (let s = 0; s < MAX_PLAYERS; ++s) {
      if (refusedBy & (1 << s)) {
        const q = this.slots[s];
        log(`room ${this.code}: turn ${turn}: every client refused commands from slot ${s} (${q?.name})`);
        this.broadcast(new W(MSG.Desync).u32(turn).u8(1 << s).str('illegal commands').done());
      }
    }
    const groups = new Map();
    for (const [slot, v] of m) groups.set(v.key, (groups.get(v.key) ?? 0) | (1 << slot));
    if (groups.size === 1) return;
    let best = 0, bestMask = 0;
    for (const mask of groups.values()) {
      const n = popcount(mask);
      if (n > best) { best = n; bestMask = mask; }
    }
    const tie = [...groups.values()].filter(mask => popcount(mask) === best).length > 1;
    const suspects = tie ? 0 : (((1 << this.max) - 1) & ~bestMask);
    log(`room ${this.code}: DESYNC at turn ${turn}: ${[...groups.entries()].map(([k, v]) => `${k}=${v.toString(2)}`).join(' ')}`);
    this.broadcast(new W(MSG.Desync).u32(turn).u8(suspects).str(tie ? 'the players disagree' : 'a player drifted').done());
    if (tie) this.stop();
    else for (let s = 0; s < MAX_PLAYERS; ++s) if (suspects & (1 << s) && this.slots[s]) this.slots[s].conn.close();
  }
}
const popcount = m => { let n = 0; for (; m; m &= m - 1) ++n; return n; };

// The live counts the menus show: players connected, rooms open.
function stats() {
  let online = 0;
  for (const p of players) if (p.hello) ++online;
  return new W(MSG.Stats).u16(Math.min(online, 65535)).u16(Math.min(rooms.size, 65534)).done();
}

function roomList() {
  const list = [...rooms.values()].filter(r => r.isPublic).slice(0, 50);
  const w = new W(MSG.RoomList).u8(list.length);
  for (const r of list) w.str(r.code).str(r.name).u8(r.players().length).u8(r.max).u8(r.inGame ? 1 : 0);
  return w.done();
}

// ------------------------------------------------------------- messages ---
function handle(p, buf) {
  const r = new R(buf);
  const type = r.u8();
  if (!p.hello && type !== MSG.Hello && type !== MSG.Ping && type !== MSG.Pong) { p.conn.close(); return; }
  const room = p.room;
  switch (type) {
    case MSG.Hello: {
      const version = r.u16(); p.build = r.u32(); p.name = [...r.str().trim()].slice(0, 24).join('') || `Player ${p.id}`;
      if (!r.ok || version !== PROTOCOL) { p.error(1, `protocol ${version}, the relay speaks ${PROTOCOL}`); p.conn.close(); return; }
      p.hello = true;
      p.send(new W(MSG.Welcome).u32(p.id).done());
      p.send(new W(MSG.Ping).u32(Date.now() >>> 0).done());   // a first round trip at once
      p.send(stats());
      debug(`player ${p.id} "${p.name}" build ${p.build.toString(16)}`);
      break;
    }
    case MSG.ListRooms: p.send(roomList()); break;
    case MSG.CreateRoom: {
      const name = r.str(), max = r.u8(), isPublic = r.u8() !== 0;
      if (room) room.remove(p);
      const nr = new Room(p, name, max || 2, isPublic);
      rooms.set(nr.code, nr);
      nr.add(p);
      log(`room ${nr.code} "${nr.name}" by ${p.name}`);
      nr.broadcastState();
      break;
    }
    case MSG.JoinRoom: {
      const nr = rooms.get(r.str().trim().toUpperCase());
      if (!nr) { p.error(2, 'no such room'); break; }
      if (nr.inGame) { p.error(2, 'the game has started'); break; }
      if (nr.build !== p.build) { p.error(2, 'that room runs a different build of the game'); break; }
      if (room) room.remove(p);
      if (!nr.add(p)) { p.error(2, 'the room is full'); break; }
      nr.broadcastState();
      break;
    }
    case MSG.LeaveRoom: if (room) room.remove(p); break;
    case MSG.Chat: {
      if (!room) break;
      if (++p.rate.chats > 20) break;
      // (Characters, not UTF-16 units: [...s] keeps emoji and the like whole.)
      room.broadcast(new W(MSG.ChatFrom).u8(p.slot).str([...r.str()].slice(0, 130).join('')).done());
      break;
    }
    case MSG.SetReady: if (room && !room.inGame) { p.ready = r.u8() !== 0; room.broadcastState(); } break;
    case MSG.SetOptions: if (room && !room.inGame && room.host === p.slot) { room.options = r.bytes(); room.broadcastState(); } break;
    case MSG.SetPlayerData: if (room && !room.inGame) { p.data = r.bytes(); room.broadcastState(); } break;
    case MSG.Start: {
      if (!room || room.inGame || room.host !== p.slot) break;
      const list = room.players();
      // A host alone may start too, as in the original (FUN_0041af20 counts only the ready).
      if (list.some(o => o !== p && !o.ready)) { p.error(4, 'not everyone is ready'); break; }
      room.start(DEFAULT_TURN_FRAMES);
      break;
    }
    case MSG.Input: {
      if (!room || !room.inGame) break;
      if (++p.rate.inputs > 120) { p.error(3, 'too many inputs'); p.conn.close(); return; }
      const turn = r.u32(), n = r.u8();
      if (n > MAX_COMMANDS) { p.conn.close(); return; }
      const cmds = [];
      for (let i = 0; i < n; ++i) cmds.push(r.raw(16));
      if (!r.ok) { p.conn.close(); return; }
      room.addInput(p, turn, cmds);
      break;
    }
    case MSG.Check: {
      if (!room || !room.inGame) break;
      const turn = r.u32(), seed = r.u32(), rolls = r.u32(), hash = r.u32(), refused = r.u8();
      if (r.ok) room.addCheck(p, turn, `${seed.toString(16)}:${rolls}:${hash.toString(16)}`, refused);
      break;
    }
    case MSG.Loaded: if (room) room.markLoaded(p); break;
    case MSG.GameOver: {
      // The battle ended for this player; once all have said so (or two
      // minutes pass) the room goes back to choosing.
      if (!room || !room.inGame) break;
      (room.over ??= new Set()).add(p.slot);
      if (room.players().every(o => room.over.has(o.slot))) { room.over = null; room.stop(); room.broadcastState(); }
      break;
    }
    case MSG.Ping: p.send(new W(MSG.Pong).u32(r.u32()).done()); break;
    case MSG.Pong: {
      // Our own ping coming back: the round trip, smoothed, and its jitter.
      const sample = ((Date.now() >>> 0) - r.u32()) >>> 0;
      if (sample >= 0 && sample < 5000) {
        // The median of the last 9 samples, and the median distance from
        // it as the jitter: a client stalled for a moment (loading a map,
        // a busy tab) does not push the whole room's input delay up.
        (p.recent ??= []).push(sample);
        if (p.recent.length > 9) p.recent.shift();
        const sorted = [...p.recent].sort((x, y) => x - y);
        p.rtt = sorted[sorted.length >> 1];
        const dev = p.recent.map(x => Math.abs(x - p.rtt)).sort((x, y) => x - y);
        p.jitter = dev[dev.length >> 1];
      }
      break;
    }
    default: p.conn.close();
  }
}

// ---------------------------------------------------------------- serve ---
const server = http.createServer((req, res) => {
  res.writeHead(200, { 'content-type': 'text/plain' });
  res.end(`Cross Hermit relay: ${rooms.size} rooms\n`);
});
const players = new Set();
server.on('upgrade', (req, socket) => {
  if (!wsAccept(req, socket)) return;
  let p = null;
  const conn = new Conn(socket, m => { try { handle(p, m); } catch (e) { log('error', e); conn.close(); } },
                        () => { players.delete(p); if (p.room) p.room.remove(p); debug(`player ${p.id} left`); });
  p = new Player(conn);
  players.add(p);
});
// Pings for the round trips; the rate limits' windows; every 5 s the counts
// for the players in the lobby.
let tick = 0;
setInterval(() => {
  const now = Date.now();
  if (++tick % 5 === 0) {
    const s = stats();
    for (const p of players) if (p.hello && !p.room) p.send(s);
  }
  for (const p of players) {
    p.send(new W(MSG.Ping).u32(now >>> 0).done());
    p.rate = { start: now, inputs: 0, chats: 0 };
  }
}, 1000);
server.listen(PORT, () => log(`relay on port ${PORT}`));
