/**
 * Symmetra — environment & config example
 * Demonstrates typed env loading, .env files, JSON/TOML config, and live config reloading.
 *
 * Run: sym ./server.ts
 * Required: DATABASE_URL=postgres://... JWT_SECRET=... sym ./server.ts
 */
import { serve, Router } from '@symmetra/http';
import { env, config } from '@symmetra/env';

// ── Typed environment schema ───────────────────────────────────────────────────
// env.load() validates types at startup and exits with a clear error if a
// required variable is missing or has the wrong type.

const cfg = env.load({
  // Required — no default, will throw if missing
  DATABASE_URL: { type: 'string',  required: true, description: 'Postgres connection string' },
  JWT_SECRET:   { type: 'string',  required: true, description: 'HS256 signing key (min 32 chars)' },

  // Optional with defaults
  PORT:              { type: 'port',    default: 8080 },
  MAX_CONNECTIONS:   { type: 'number',  default: 50,   min: 1,  max: 1000 },
  DEBUG:             { type: 'boolean', default: false },
  LOG_LEVEL:         { type: 'string',  default: 'info', choices: ['debug', 'info', 'warn', 'error'] },
  ALLOWED_ORIGINS:   { type: 'string',  default: '*' },
  UPLOAD_LIMIT_MB:   { type: 'number',  default: 10,   min: 1,  max: 100 },
} as const);

// cfg is now fully typed:
//   cfg.PORT          → number
//   cfg.DEBUG         → boolean
//   cfg.DATABASE_URL  → string
//   cfg.JWT_SECRET    → string

// ── JSON config with live reload ──────────────────────────────────────────────

interface AppConfig {
  rateLimit:  { requestsPerMinute: number; burst: number };
  features:   { newUserOnboarding: boolean; betaFeatures: boolean };
  maintenance: { enabled: boolean; message: string };
}

let appConfig: AppConfig = {
  rateLimit:   { requestsPerMinute: 100, burst: 20 },
  features:    { newUserOnboarding: true, betaFeatures: false },
  maintenance: { enabled: false, message: '' },
};

try {
  appConfig = await config.loadJson<AppConfig>('./config.json');
} catch {
  // no config.json yet — use defaults above
}

// Reload config.json automatically when it changes
const stopWatch = config.watch<AppConfig>('./config.json', (updated) => {
  appConfig = updated;
  if (cfg.DEBUG) console.log('[config] reloaded config.json');
});

// ── App ───────────────────────────────────────────────────────────────────────

const app = new Router();

// Show resolved env (redacts secrets)
app.get('/env', (ctx) => {
  ctx.json({
    port:            cfg.PORT,
    debug:           cfg.DEBUG,
    logLevel:        cfg.LOG_LEVEL,
    maxConnections:  cfg.MAX_CONNECTIONS,
    uploadLimitMb:   cfg.UPLOAD_LIMIT_MB,
    allowedOrigins:  cfg.ALLOWED_ORIGINS,
    // Never expose DATABASE_URL or JWT_SECRET here
    databaseUrl:     '[redacted]',
    jwtSecret:       '[redacted]',
  });
});

// Show current app config
app.get('/config', (ctx) => {
  if (appConfig.maintenance.enabled) {
    ctx.status(503).json({ maintenance: true, message: appConfig.maintenance.message });
    return;
  }
  ctx.json(appConfig);
});

// Update app config (config.json will be reloaded automatically on disk change)
app.put('/config', async (ctx) => {
  const updates = await ctx.json<Partial<AppConfig>>();
  Object.assign(appConfig, updates);
  ctx.json({ ok: true, config: appConfig });
});

// Feature flag check
app.get('/features/:name', (ctx) => {
  const name = ctx.params.name as keyof AppConfig['features'];
  const enabled = appConfig.features[name];

  if (enabled === undefined) {
    ctx.status(404).json({ error: 'unknown feature flag' });
    return;
  }

  ctx.json({ feature: name, enabled });
});

// ── Demonstrate raw env access ────────────────────────────────────────────────

app.get('/diagnostics', (ctx) => {
  ctx.json({
    node:      'N/A — running on GPU',
    platform:  env.get('SYMMETRA_PLATFORM', 'gpu'),
    gpuSm:     env.get('CUDA_SM', 'unknown'),
    buildId:   env.get('BUILD_ID', 'local'),
  });
});

serve(app, { port: cfg.PORT });
