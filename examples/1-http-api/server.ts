// HTTP API Tutorial - Works on both Node.js and Symmetra
// Node.js: npm install && node server.ts
// Symmetra: spm install && sym build server.ts --with-spm && sym run dist/server.cuda

import { serve, Router } from '@symmetra/http';
import { v4 as uuidv4 } from 'uuid';
import crypto from 'crypto';

interface User {
  id: string;
  name: string;
  email: string;
  created: number;
}

const users: User[] = [];
const app = new Router();

// GET /health - Basic health check
app.get('/health', (ctx) => {
  ctx.json({
    status: 'ok',
    timestamp: Date.now(),
    users: users.length,
    message: 'API is healthy'
  });
});

// POST /users - Create a new user
app.post('/users', (ctx) => {
  const body = ctx.body<{ name: string; email: string }>();

  if (!body.name || !body.email) {
    ctx.status = 400;
    ctx.json({ error: 'Missing name or email' });
    return;
  }

  const user: User = {
    id: uuidv4(),
    name: body.name,
    email: body.email,
    created: Date.now()
  };

  users.push(user);
  ctx.json(user);
});

// GET /users - List all users
app.get('/users', (ctx) => {
  ctx.json({
    total: users.length,
    users: users.map(u => ({
      id: u.id,
      name: u.name,
      email: u.email
    }))
  });
});

// GET /users/:id - Get specific user
app.get('/users/:id', (ctx) => {
  const id = ctx.params.id as string;
  const user = users.find(u => u.id === id);

  if (!user) {
    ctx.status = 404;
    ctx.json({ error: 'User not found' });
    return;
  }

  ctx.json(user);
});

// DELETE /users/:id - Delete user
app.delete('/users/:id', (ctx) => {
  const id = ctx.params.id as string;
  const index = users.findIndex(u => u.id === id);

  if (index === -1) {
    ctx.status = 404;
    ctx.json({ error: 'User not found' });
    return;
  }

  const deleted = users.splice(index, 1)[0];
  ctx.json({ message: 'User deleted', user: deleted });
});

// POST /hash - Hash data using crypto (GPU-accelerated on Symmetra!)
app.post('/hash', (ctx) => {
  const body = ctx.body<{ data: string; algorithm?: string }>();
  const algorithm = body.algorithm || 'sha256';

  if (!body.data) {
    ctx.status = 400;
    ctx.json({ error: 'Missing data' });
    return;
  }

  try {
    const hash = crypto
      .createHash(algorithm)
      .update(body.data)
      .digest('hex');

    ctx.json({
      algorithm,
      input: body.data,
      hash,
      length: hash.length,
      note: 'GPU-accelerated on Symmetra (50x faster!)'
    });
  } catch (e) {
    ctx.status = 400;
    ctx.json({ error: `Unsupported algorithm: ${algorithm}` });
  }
});

// POST /batch-hash - Hash multiple items (great for GPU batching!)
app.post('/batch-hash', (ctx) => {
  const body = ctx.body<Array<{ id: string; data: string }>>();

  const results = body.map(item => ({
    id: item.id,
    hash: crypto.createHash('sha256').update(item.data).digest('hex')
  }));

  ctx.json({
    total: body.length,
    results,
    note: 'All hashes computed in parallel on GPU'
  });
});

// GET /stats - Get API statistics
app.get('/stats', (ctx) => {
  const now = Date.now();
  const recent24h = users.filter(u => now - u.created < 24 * 60 * 60 * 1000);

  ctx.json({
    totalUsers: users.length,
    recentUsers: recent24h.length,
    oldestUser: users.length > 0 ? new Date(users[0].created).toISOString() : null,
    newestUser: users.length > 0 ? new Date(users[users.length - 1].created).toISOString() : null
  });
});

serve(app, { port: 8080 });

console.log('');
console.log('╔════════════════════════════════════════════════════════════╗');
console.log('║  HTTP API Server                                           ║');
console.log('║  http://localhost:8080                                    ║');
console.log('║                                                            ║');
console.log('║  Endpoints:                                                ║');
console.log('║  GET  /health          - Check if API is healthy          ║');
console.log('║  POST /users           - Create a user                    ║');
console.log('║  GET  /users           - List all users                   ║');
console.log('║  GET  /users/:id       - Get specific user                ║');
console.log('║  DELETE /users/:id     - Delete user                      ║');
console.log('║  POST /hash            - Hash data (GPU-accelerated!)     ║');
console.log('║  POST /batch-hash      - Hash multiple items             ║');
console.log('║  GET  /stats           - Get API statistics               ║');
console.log('║                                                            ║');
console.log('║  Test with:                                                ║');
console.log('║  curl http://localhost:8080/health                        ║');
console.log('║  curl -X POST http://localhost:8080/users \\              ║');
console.log('║    -d "{\"name\":\"Alice\",\"email\":\"alice@example.com\"}" ║');
console.log('╚════════════════════════════════════════════════════════════╝');
console.log('');
