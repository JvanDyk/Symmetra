# `@symmetra/crypto` — Crypto + JWT Reference

GPU-accelerated cryptographic operations. Hash, sign, verify, and issue JWTs — all without leaving VRAM.

## Installation

```typescript
import { hash, hmac, sign, verify, jwt } from '@symmetra/crypto';
```

---

## Hash functions

### `hash(algorithm: HashAlgorithm, data: string | Uint8Array): Promise<string>`

Compute a cryptographic hash. Returns a lowercase hex string.

```typescript
const sha256 = await hash('SHA-256', 'hello world');
// "b94d27b9934d3e08a52e52d7da7dabfac484efe04294e576b4c5b3af31b04d83"

const md5 = await hash('MD5', 'hello');
// "5d41402abc4b2a76b9719d911017c592"
```

#### Supported algorithms

| Algorithm | Output length |
|-----------|---------------|
| `'SHA-1'` | 40 hex chars (20 bytes) |
| `'SHA-256'` | 64 hex chars (32 bytes) |
| `'SHA-384'` | 96 hex chars (48 bytes) |
| `'SHA-512'` | 128 hex chars (64 bytes) |
| `'MD5'` | 32 hex chars (16 bytes) |
| `'BLAKE3'` | 64 hex chars (32 bytes) |

---

## HMAC

### `hmac(algorithm: HashAlgorithm, key: string, data: string): Promise<string>`

Compute an HMAC. Returns lowercase hex.

```typescript
const sig = await hmac('SHA-256', process.env.SECRET!, payload);
```

---

## Symmetric encryption

### `encrypt(algorithm: 'AES-256-GCM', key: string, plaintext: string): Promise<EncryptResult>`

Encrypt a string. The key is stretched with PBKDF2 (100k iterations, SHA-256) if not 32 bytes.

```typescript
const { ciphertext, iv, tag } = await encrypt('AES-256-GCM', key, 'secret data');
```

#### `EncryptResult`

| Field | Type | Description |
|-------|------|-------------|
| `ciphertext` | `string` | Base64-encoded ciphertext |
| `iv` | `string` | Base64-encoded initialization vector (12 bytes) |
| `tag` | `string` | Base64-encoded authentication tag (16 bytes) |

### `decrypt(algorithm: 'AES-256-GCM', key: string, payload: EncryptResult): Promise<string>`

Decrypt a previously encrypted payload. Throws `CryptoError` if the tag is invalid.

```typescript
const plaintext = await decrypt('AES-256-GCM', key, { ciphertext, iv, tag });
```

---

## Random values

### `randomBytes(n: number): Uint8Array`

Generate `n` cryptographically secure random bytes using the GPU's hardware RNG.

```typescript
const id = randomBytes(16);
const hex = Array.from(id).map(b => b.toString(16).padStart(2, '0')).join('');
```

### `randomUUID(): string`

Generate a RFC 4122 version 4 UUID.

```typescript
const id = randomUUID();
// "550e8400-e29b-41d4-a716-446655440000"
```

---

## JWT

### `jwt.sign(payload: object, secret: string, options?: JwtSignOptions): Promise<string>`

Create a signed JWT (HS256 by default).

```typescript
const token = await jwt.sign(
  { sub: 'user:42', role: 'admin' },
  process.env.JWT_SECRET!,
  { expiresIn: '1h' },
);
```

#### `JwtSignOptions`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `algorithm` | `'HS256' \| 'HS384' \| 'HS512'` | `'HS256'` | Signing algorithm |
| `expiresIn` | `string \| number` | none | Expiry — string like `'1h'`, `'7d'`, or seconds |
| `issuer` | `string` | — | `iss` claim |
| `audience` | `string` | — | `aud` claim |
| `subject` | `string` | — | `sub` claim |

### `jwt.verify<T>(token: string, secret: string, options?: JwtVerifyOptions): Promise<T>`

Verify a JWT and return its decoded payload. Throws `JwtError` if invalid, expired, or tampered.

```typescript
try {
  const payload = await jwt.verify<{ sub: string; role: string }>(
    token,
    process.env.JWT_SECRET!,
  );
  ctx.json({ user: payload.sub });
} catch (e) {
  ctx.status(401).json({ error: 'unauthorized' });
}
```

#### `JwtVerifyOptions`

| Field | Type | Description |
|-------|------|-------------|
| `algorithms` | `string[]` | Allowed algorithms (default: `['HS256']`) |
| `issuer` | `string` | Expected `iss` value |
| `audience` | `string` | Expected `aud` value |

### `jwt.decode<T>(token: string): T`

Decode a JWT **without** verifying the signature. Use only for inspecting untrusted tokens before a full verify.

```typescript
const { sub, exp } = jwt.decode<{ sub: string; exp: number }>(token);
```

---

## Password hashing

### `bcrypt.hash(password: string, rounds?: number): Promise<string>`

Hash a password with bcrypt.

```typescript
const hash = await bcrypt.hash('mypassword', 12);
```

### `bcrypt.verify(password: string, hash: string): Promise<boolean>`

Compare a plaintext password against a bcrypt hash.

```typescript
const ok = await bcrypt.verify(ctx.body.password, storedHash);
if (!ok) {
  ctx.status(401).json({ error: 'invalid password' });
  return;
}
```

---

## Error types

| Class | When thrown |
|-------|-------------|
| `CryptoError` | Invalid key, algorithm not supported, decrypt auth failure |
| `JwtError` | Invalid signature, expired token, malformed JWT |

---

## Complete example — JWT auth middleware

```typescript
import { serve, Router } from '@symmetra/http';
import { jwt, hash, randomUUID } from '@symmetra/crypto';
import { KvStore } from '@symmetra/kv';

const app = new Router();
const db  = new KvStore({ namespace: 'users', maxMb: 64 });
const SECRET = process.env.JWT_SECRET ?? 'change-me';

// POST /login  { username, password } → { token }
app.post('/login', async (ctx) => {
  const { username, password } = await ctx.json<{ username: string; password: string }>();
  const user = await db.get<{ hash: string }>(username);
  if (!user) {
    ctx.status(401).json({ error: 'invalid credentials' });
    return;
  }
  const pwHash = await hash('SHA-256', password);
  if (pwHash !== user.hash) {
    ctx.status(401).json({ error: 'invalid credentials' });
    return;
  }
  const token = await jwt.sign({ sub: username }, SECRET, { expiresIn: '8h' });
  ctx.json({ token });
});

// GET /me  Authorization: Bearer <token> → { username }
app.get('/me', async (ctx) => {
  const auth = ctx.headers['authorization'] ?? '';
  const token = auth.replace('Bearer ', '');
  try {
    const payload = await jwt.verify<{ sub: string }>(token, SECRET);
    ctx.json({ username: payload.sub });
  } catch {
    ctx.status(401).json({ error: 'unauthorized' });
  }
});

serve(app, { port: 8080 });
```
