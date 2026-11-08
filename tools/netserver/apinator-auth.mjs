// ============================================================================
// apinator-auth.mjs — the channel signer for the Apinator transport, so the
// app's secret never has to be built into the game.
//
// The game POSTs {socket_id, channel_name, channel_data?} and gets back
// {auth: "<key>:<hex HMAC-SHA256(secret, socket_id:channel[:channel_data])>",
// channel_data?}. Only the game's own channels are signed (private-ch-* and
// presence-ch-*), and a presence member must be the connection itself
// (user_id == socket_id), so nobody can sign in as another player.
//
// Deploy as a Cloudflare Worker (free tier is plenty): this file is the
// worker; set APINATOR_KEY and APINATOR_SECRET as its secrets
// (`wrangler secret put APINATOR_SECRET`). Or run it with Node 18+:
//   node tools/netserver/apinator-auth.mjs [--port 8788] [--env tools/netserver/apinator-us.env]
// Then put APINATOR_AUTH_URL=https://<worker>/ in the region's .env file (the
// build then leaves the secret out) and rebuild.
// ============================================================================

const enc = new TextEncoder();
const hex = buf => [...new Uint8Array(buf)].map(b => b.toString(16).padStart(2, '0')).join('');
const CORS = {
  'access-control-allow-origin': '*',
  'access-control-allow-methods': 'POST, OPTIONS',
  'access-control-allow-headers': 'content-type',
};
const reply = (status, body) =>
  new Response(JSON.stringify(body), { status, headers: { 'content-type': 'application/json', ...CORS } });

export async function sign(secret, text) {
  const key = await crypto.subtle.importKey('raw', enc.encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  return hex(await crypto.subtle.sign('HMAC', key, enc.encode(text)));
}

const worker = {
  async fetch(request, env) {
    if (request.method === 'OPTIONS') return new Response(null, { status: 204, headers: CORS });
    if (request.method !== 'POST') return reply(405, { error: 'POST only' });
    let q;
    try {
      const text = await request.text();
      if (text.length > 2048) return reply(413, { error: 'too large' });
      q = request.headers.get('content-type')?.includes('json') ? JSON.parse(text) : Object.fromEntries(new URLSearchParams(text));
    } catch { return reply(400, { error: 'bad request' }); }
    const socket = String(q.socket_id ?? ''), channel = String(q.channel_name ?? '');
    if (!/^[\w.\-:]{1,64}$/.test(socket)) return reply(400, { error: 'bad socket_id' });
    if (!/^(private|presence)-ch-[\w\-]{1,48}$/.test(channel)) return reply(403, { error: 'not a game channel' });
    let toSign = `${socket}:${channel}`;
    const out = {};
    if (channel.startsWith('presence-')) {
      let member;
      try { member = JSON.parse(String(q.channel_data ?? '')); } catch { return reply(400, { error: 'bad channel_data' }); }
      if (member?.user_id !== socket) return reply(403, { error: 'user_id must be the socket_id' });
      out.channel_data = JSON.stringify({ user_id: socket, user_info: member.user_info ?? {} });
      if (out.channel_data.length > 512) return reply(413, { error: 'user_info too large' });
      toSign += `:${out.channel_data}`;
    }
    out.auth = `${env.APINATOR_KEY}:${await sign(env.APINATOR_SECRET, toSign)}`;
    return reply(200, out);
  },
};
export default worker;

// ------------------------------------------------------------ Node 18+ ---
if (typeof process !== 'undefined' && process.argv[1] && import.meta.url.endsWith(process.argv[1].split('/').pop())) {
  const { createServer } = await import('node:http');
  const { readFileSync } = await import('node:fs');
  const args = process.argv.slice(2);
  const opt = (name, dflt) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : dflt; };
  const env = { APINATOR_KEY: process.env.APINATOR_KEY, APINATOR_SECRET: process.env.APINATOR_SECRET };
  const file = opt('--env');
  if (file) {
    for (const line of readFileSync(file, 'utf8').split('\n')) {
      const m = /^(APINATOR_[A-Z_]+)=(.*)$/.exec(line.trim());
      if (m) env[m[1]] = m[2].trim();
    }
  }
  if (!env.APINATOR_KEY || !env.APINATOR_SECRET) { console.error('APINATOR_KEY and APINATOR_SECRET are needed'); process.exit(1); }
  const port = Number(opt('--port', process.env.PORT ?? 8788));
  createServer(async (req, res) => {
    const chunks = [];
    for await (const c of req) chunks.push(c);
    const r = await worker.fetch(new Request(`http://localhost${req.url}`, {
      method: req.method, headers: req.headers, body: ['GET', 'HEAD'].includes(req.method) ? undefined : Buffer.concat(chunks),
    }), env);
    res.writeHead(r.status, Object.fromEntries(r.headers));
    res.end(Buffer.from(await r.arrayBuffer()));
  }).listen(port, () => console.log(`apinator auth on port ${port}`));
}
