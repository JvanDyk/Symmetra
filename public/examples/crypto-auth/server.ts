/**
 * Symmetra — crypto & JWT auth example
 * Demonstrates hashing, encryption, password auth, JWT tokens, and secure random.
 *
 * Run: sym ./server.ts
 */
import { serve, Router } from '@symmetra/http';
import { hash, cipher, jwt, password, random, kdf } from '@symmetra/crypto';
import { postgres } from '@symmetra/db';
import { KvStore } from '@symmetra/kv';
import { env } from '@symmetra/env';

const cfg = env.load({
  JWT_SECRET:  { type: 'string', required: true },
  DB_HOST:     { type: 'string', required: true },
  DB_USER:     { type: 'string', required: true },
  DB_PASS:     { type: 'string', required: true },
  DB_NAME:     { type: 'string', required: true },
  PORT:        { type: 'port',   default: 8080 },
} as const);

const pg = postgres({
  host:     cfg.DB_HOST,
  database: cfg.DB_NAME,
  user:     cfg.DB_USER,
  password: cfg.DB_PASS,
});

const sessions = new KvStore({ maxMb: 64 });

const app = new Router();

// ── Register ──────────────────────────────────────────────────────────────────

app.post('/auth/register', async (ctx) => {
  const { email, passwordRaw } = await ctx.json<{ email: string; passwordRaw: string }>();

  if (!email || !passwordRaw || passwordRaw.length < 8) {
    ctx.status(400).json({ error: 'email and password (min 8 chars) required' });
    return;
  }

  const existing = await pg.queryOne('SELECT id FROM users WHERE email = $1', [email]);
  if (existing) {
    ctx.status(409).json({ error: 'email already registered' });
    return;
  }

  const hashed = await password.hash(passwordRaw);

  const row = await pg.queryOne<{ id: number }>(
    'INSERT INTO users (email, password_hash) VALUES ($1, $2) RETURNING id',
    [email, hashed]
  );

  ctx.status(201).json({ id: row!.id, email });
});

// ── Login ─────────────────────────────────────────────────────────────────────

app.post('/auth/login', async (ctx) => {
  const { email, passwordRaw } = await ctx.json<{ email: string; passwordRaw: string }>();

  const user = await pg.queryOne<{ id: number; email: string; password_hash: string }>(
    'SELECT id, email, password_hash FROM users WHERE email = $1',
    [email]
  );

  if (!user || !await password.verify(passwordRaw, user.password_hash)) {
    ctx.status(401).json({ error: 'invalid credentials' });
    return;
  }

  const token = await jwt.sign(
    { sub: String(user.id), email: user.email },
    cfg.JWT_SECRET,
    { expiresIn: 86400, algorithm: 'HS256' }
  );

  ctx.json({ token });
});

// ── Auth middleware ───────────────────────────────────────────────────────────

async function requireAuth(ctx: any): Promise<{ id: number; email: string } | null> {
  const authHeader = ctx.header('authorization') as string | undefined;
  if (!authHeader?.startsWith('Bearer ')) return null;

  try {
    const payload = await jwt.verify<{ sub: string; email: string }>(
      authHeader.slice(7),
      cfg.JWT_SECRET
    );
    return { id: parseInt(payload.sub, 10), email: payload.email };
  } catch {
    return null;
  }
}

// ── Protected route example ───────────────────────────────────────────────────

app.get('/me', async (ctx) => {
  const user = await requireAuth(ctx);
  if (!user) {
    ctx.status(401).json({ error: 'unauthorized' });
    return;
  }
  ctx.json(user);
});

// ── Data encryption example ───────────────────────────────────────────────────

const DATA_KEY = await kdf.hkdf(cfg.JWT_SECRET, 'symmetra-data-key', { keyLength: 32 });

app.post('/vault/store', async (ctx) => {
  const user = await requireAuth(ctx);
  if (!user) { ctx.status(401).json({ error: 'unauthorized' }); return; }

  const { secret } = await ctx.json<{ secret: string }>();
  const encrypted  = await cipher.encryptString('aes-256-gcm', DATA_KEY, secret);

  await pg.query(
    'INSERT INTO vault (user_id, encrypted_data) VALUES ($1, $2) ON CONFLICT (user_id) DO UPDATE SET encrypted_data = $2',
    [user.id, encrypted]
  );

  ctx.json({ stored: true });
});

app.get('/vault/retrieve', async (ctx) => {
  const user = await requireAuth(ctx);
  if (!user) { ctx.status(401).json({ error: 'unauthorized' }); return; }

  const row = await pg.queryOne<{ encrypted_data: string }>(
    'SELECT encrypted_data FROM vault WHERE user_id = $1',
    [user.id]
  );

  if (!row) { ctx.status(404).json({ error: 'nothing stored' }); return; }

  const decrypted = await cipher.decryptString('aes-256-gcm', DATA_KEY, row.encrypted_data);
  ctx.json({ secret: decrypted });
});

// ── Hashing / checksums ───────────────────────────────────────────────────────

app.post('/hash', async (ctx) => {
  const { data, algorithm } = await ctx.json<{ data: string; algorithm?: 'sha256' | 'sha512' | 'blake3' }>();
  const digest = await hash.hash(algorithm ?? 'sha256', data);
  ctx.json({ hash: digest, algorithm: algorithm ?? 'sha256' });
});

// ── Secure token generation ───────────────────────────────────────────────────

app.post('/tokens/generate', async (ctx) => {
  const user = await requireAuth(ctx);
  if (!user) { ctx.status(401).json({ error: 'unauthorized' }); return; }

  const { purpose, ttlSeconds } = await ctx.json<{ purpose: string; ttlSeconds?: number }>();
  const token   = random.hex(32);
  const ttl     = ttlSeconds ?? 3600;

  await sessions.set(`otp:${token}`, { userId: user.id, purpose }, { ttlMs: ttl * 1000 });

  ctx.json({ token, expiresIn: ttl });
});

app.post('/tokens/redeem', async (ctx) => {
  const { token } = await ctx.json<{ token: string }>();
  const stored = await sessions.get<{ userId: number; purpose: string }>(`otp:${token}`);

  if (!stored) {
    ctx.status(404).json({ error: 'token not found or expired' });
    return;
  }

  await sessions.delete(`otp:${token}`);
  ctx.json({ userId: stored.userId, purpose: stored.purpose });
});

serve(app, { port: cfg.PORT });
