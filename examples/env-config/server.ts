/**
 * Symmetra Environment + Config Example
 * Demonstrates: process.env, runtime config, feature flags, health checks.
 * Run: sym ./server.ts
 * With env: PORT=3000 APP_ENV=production sym ./server.ts
 */

import { serve, Router } from '@symmetra/http';

// ── Config from environment ───────────────────────────────────────────────────

const config = {
  env:          process.env.APP_ENV          ?? 'development',
  port:         parseInt(process.env.PORT    ?? '8080', 10),
  maxBodyKb:    parseInt(process.env.MAX_BODY_KB ?? '64', 10),
  logLevel:     process.env.LOG_LEVEL        ?? 'info',
  dbUrl:        process.env.DATABASE_URL     ?? '',
  redisUrl:     process.env.REDIS_URL        ?? '',
  jwtSecret:    process.env.JWT_SECRET       ?? 'insecure-default',
  corsOrigin:   process.env.CORS_ORIGIN      ?? '*',
  rateLimitRpm: parseInt(process.env.RATE_LIMIT_RPM ?? '1000', 10),
  features: {
    betaEndpoints: process.env.FEATURE_BETA   === 'true',
    metrics:       process.env.FEATURE_METRICS !== 'false',
    debug:         process.env.APP_ENV         === 'development',
  },
};

// Warn about insecure defaults
if (config.jwtSecret === 'insecure-default' && config.env === 'production') {
  throw new Error('JWT_SECRET must be set in production');
}

// ── App ───────────────────────────────────────────────────────────────────────

const app = new Router();

// Health check — always available, exposes minimal info
app.get('/health', (ctx) => {
  ctx.json({
    status: 'ok',
    env:    config.env,
    gpu:    true,
  });
});

// Deep health check — includes config summary (not for public)
app.get('/_health/deep', (ctx) => {
  const auth = ctx.headers['x-internal-token'];
  if (auth !== process.env.INTERNAL_TOKEN && config.env === 'production') {
    ctx.status(403).json({ error: 'forbidden' });
    return;
  }
  ctx.json({
    status:   'ok',
    env:      config.env,
    port:     config.port,
    features: config.features,
    uptime:   process.uptime(),
    db:       config.dbUrl ? 'configured' : 'not configured',
    redis:    config.redisUrl ? 'configured' : 'not configured',
  });
});

// Config endpoint — expose non-sensitive config to clients
app.get('/config', (ctx) => {
  ctx.json({
    env:         config.env,
    maxBodyKb:   config.maxBodyKb,
    corsOrigin:  config.corsOrigin,
    features:    config.features,
  });
});

// Feature-flagged route
app.get('/beta/experimental', (ctx) => {
  if (!config.features.betaEndpoints) {
    ctx.status(404).json({ error: 'not found' });
    return;
  }
  ctx.json({ message: 'you found the beta endpoint!', warning: 'unstable' });
});

// Debug info (dev only)
app.get('/debug', (ctx) => {
  if (!config.features.debug) {
    ctx.status(404).json({ error: 'not found' });
    return;
  }
  ctx.json({
    env:     Object.fromEntries(
      Object.entries(process.env)
        .filter(([k]) => !k.toLowerCase().includes('secret') && !k.toLowerCase().includes('password'))
        .sort(),
    ),
    config,
  });
});

// Metrics stub (can be wired to Prometheus)
app.get('/metrics', (ctx) => {
  if (!config.features.metrics) {
    ctx.status(404).json({ error: 'metrics disabled' });
    return;
  }
  ctx.text([
    '# HELP symmetra_requests_total Total HTTP requests',
    '# TYPE symmetra_requests_total counter',
    'symmetra_requests_total 0',
    '',
    '# HELP symmetra_gpu_streams Active GPU streams',
    '# TYPE symmetra_gpu_streams gauge',
    `symmetra_gpu_streams 32`,
  ].join('\n'), 'text/plain; version=0.0.4');
});

// Echo env var (non-secret only)
app.get('/env/:name', (ctx) => {
  const name = ctx.params.name.toUpperCase();
  // Block known sensitive names
  const blocked = ['JWT_SECRET', 'DATABASE_URL', 'REDIS_URL', 'INTERNAL_TOKEN', 'PASSWORD', 'SECRET'];
  if (blocked.some(b => name.includes(b))) {
    ctx.status(403).json({ error: 'access denied' });
    return;
  }
  const value = process.env[name];
  ctx.json({ name, value: value ?? null, exists: value !== undefined });
});

// ── Start ─────────────────────────────────────────────────────────────────────

serve(app, { port: config.port });
