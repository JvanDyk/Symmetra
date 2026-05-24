/**
 * Symmetra Crypto + Auth Example
 * Demonstrates: JWT auth, bcrypt password hashing, AES encryption, random IDs.
 * Run: sym ./server.ts
 */

import { serve, Router } from '@symmetra/http';
import { hash, hmac, encrypt, decrypt, jwt, bcrypt, randomUUID, randomBytes } from '@symmetra/crypto';
import { KvStore } from '@symmetra/kv';

const app = new Router();
const db  = new KvStore({ namespace: 'auth-demo', maxMb: 64 });

const SECRET = process.env.JWT_SECRET ?? 'symmetra-demo-secret-change-in-prod';

interface StoredUser {
  id:       string;
  username: string;
  pwHash:   string;
  role:     string;
}

// ── Registration ──────────────────────────────────────────────────────────────

app.post('/register', async (ctx) => {
  const { username, password } = await ctx.json<{ username: string; password: string }>();

  if (!username || !password || password.length < 8) {
    ctx.status(400).json({ error: 'username required; password min 8 chars' });
    return;
  }

  if (await db.has(`user:${username}`)) {
    ctx.status(409).json({ error: 'username already taken' });
    return;
  }

  const id     = randomUUID();
  const pwHash = await bcrypt.hash(password, 12);

  await db.set(`user:${username}`, { id, username, pwHash, role: 'user' } satisfies StoredUser);

  ctx.status(201).json({ id, username });
});

// ── Login → JWT ───────────────────────────────────────────────────────────────

app.post('/login', async (ctx) => {
  const { username, password } = await ctx.json<{ username: string; password: string }>();

  const user = await db.get<StoredUser>(`user:${username}`);
  if (!user) {
    ctx.status(401).json({ error: 'invalid credentials' });
    return;
  }

  const ok = await bcrypt.verify(password, user.pwHash);
  if (!ok) {
    ctx.status(401).json({ error: 'invalid credentials' });
    return;
  }

  const token = await jwt.sign(
    { sub: user.id, username: user.username, role: user.role },
    SECRET,
    { expiresIn: '8h' },
  );

  ctx.json({ token, expiresIn: '8h' });
});

// ── Auth middleware helper ────────────────────────────────────────────────────

async function requireAuth(ctx: SymmCtx): Promise<{ sub: string; username: string; role: string } | null> {
  const auth = ctx.headers['authorization'] ?? '';
  if (!auth.startsWith('Bearer ')) return null;
  try {
    return await jwt.verify<{ sub: string; username: string; role: string }>(
      auth.slice(7), SECRET,
    );
  } catch {
    return null;
  }
}

// ── Protected routes ──────────────────────────────────────────────────────────

app.get('/me', async (ctx) => {
  const user = await requireAuth(ctx);
  if (!user) { ctx.status(401).json({ error: 'unauthorized' }); return; }
  ctx.json({ id: user.sub, username: user.username, role: user.role });
});

app.get('/admin', async (ctx) => {
  const user = await requireAuth(ctx);
  if (!user) { ctx.status(401).json({ error: 'unauthorized' }); return; }
  if (user.role !== 'admin') { ctx.status(403).json({ error: 'forbidden' }); return; }
  ctx.json({ secret: 'admin panel data' });
});

// ── Hashing demos ─────────────────────────────────────────────────────────────

app.post('/hash', async (ctx) => {
  const { text, algorithm } = await ctx.json<{ text: string; algorithm?: string }>();
  const algo = (algorithm ?? 'SHA-256') as 'SHA-256' | 'SHA-512' | 'MD5' | 'BLAKE3';
  const digest = await hash(algo, text);
  ctx.json({ algorithm: algo, input: text, digest });
});

app.post('/hmac', async (ctx) => {
  const { text, key } = await ctx.json<{ text: string; key: string }>();
  const sig = await hmac('SHA-256', key, text);
  ctx.json({ signature: sig });
});

// ── Encryption ────────────────────────────────────────────────────────────────

app.post('/encrypt', async (ctx) => {
  const { plaintext, key } = await ctx.json<{ plaintext: string; key: string }>();
  const result = await encrypt('AES-256-GCM', key, plaintext);
  ctx.json(result);
});

app.post('/decrypt', async (ctx) => {
  const { ciphertext, iv, tag, key } = await ctx.json<{
    ciphertext: string; iv: string; tag: string; key: string;
  }>();
  try {
    const plaintext = await decrypt('AES-256-GCM', key, { ciphertext, iv, tag });
    ctx.json({ plaintext });
  } catch {
    ctx.status(400).json({ error: 'decryption failed — wrong key or tampered data' });
  }
});

// ── Random ID / token generation ──────────────────────────────────────────────

app.get('/random/uuid', (ctx) => {
  ctx.json({ uuid: randomUUID() });
});

app.get('/random/token/:bytes', (ctx) => {
  const n     = Math.min(parseInt(ctx.params.bytes, 10) || 32, 256);
  const bytes = randomBytes(n);
  const hex   = Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('');
  ctx.json({ bytes: n, hex });
});

// ── Start ─────────────────────────────────────────────────────────────────────

serve(app, { port: 8080 });
