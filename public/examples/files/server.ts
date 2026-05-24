/**
 * Symmetra — file I/O example
 * Demonstrates reading, writing, watching files and serving static assets.
 *
 * Run: sym ./server.ts
 */
import { serve, Router } from '@symmetra/http';
import { fs, jsonFs } from '@symmetra/fs';
import { env } from '@symmetra/env';

const cfg = env.load({
  PORT:       { type: 'port',   default: 8080 },
  UPLOAD_DIR: { type: 'string', default: './uploads' },
  DATA_DIR:   { type: 'string', default: './data' },
} as const);

// Create directories on startup if they don't exist
await fs.mkdir(cfg.UPLOAD_DIR, { recursive: true });
await fs.mkdir(cfg.DATA_DIR,   { recursive: true });

const app = new Router();

// ── Static file serving ───────────────────────────────────────────────────────

app.get('/static/:filename', async (ctx) => {
  const name = ctx.params.filename;

  if (name.includes('..') || name.includes('/')) {
    ctx.status(400).json({ error: 'invalid filename' });
    return;
  }

  const path = `./public/${name}`;

  if (!await fs.exists(path)) {
    ctx.status(404).json({ error: 'not found' });
    return;
  }

  const stat = await fs.stat(path);
  const content = await fs.read(path, { encoding: 'binary' });

  ctx.header('Content-Length', String(stat.size));
  ctx.header('Last-Modified', stat.modifiedAt.toUTCString());
  ctx.send(content);
});

// ── File upload ───────────────────────────────────────────────────────────────

app.post('/upload/:name', async (ctx) => {
  const name = ctx.params.name;

  if (name.includes('..') || name.includes('/')) {
    ctx.status(400).json({ error: 'invalid filename' });
    return;
  }

  const data = await ctx.body<Uint8Array>();
  const dest  = `${cfg.UPLOAD_DIR}/${name}`;

  await fs.write(dest, data);

  const stat = await fs.stat(dest);
  ctx.json({ name, size: stat.size, path: dest });
});

// ── Download a file ───────────────────────────────────────────────────────────

app.get('/download/:name', async (ctx) => {
  const name = ctx.params.name;
  const path = `${cfg.UPLOAD_DIR}/${name}`;

  if (!await fs.exists(path)) {
    ctx.status(404).json({ error: 'not found' });
    return;
  }

  const data = await fs.read(path, { encoding: 'binary' });
  ctx.header('Content-Disposition', `attachment; filename="${name}"`);
  ctx.send(data);
});

// ── List uploaded files ───────────────────────────────────────────────────────

app.get('/uploads', async (ctx) => {
  const entries = await fs.readdir(cfg.UPLOAD_DIR);

  const files = await Promise.all(
    entries
      .filter(e => e.type === 'file')
      .map(async e => {
        const stat = await fs.stat(`${cfg.UPLOAD_DIR}/${e.name}`);
        return { name: e.name, size: stat.size, modifiedAt: stat.modifiedAt };
      })
  );

  ctx.json(files);
});

// ── JSON config read / write ──────────────────────────────────────────────────

app.get('/config', async (ctx) => {
  const path = `${cfg.DATA_DIR}/config.json`;

  if (!await fs.exists(path)) {
    ctx.json({});
    return;
  }

  const config = await jsonFs.read<Record<string, unknown>>(path);
  ctx.json(config);
});

app.put('/config', async (ctx) => {
  const body = await ctx.json<Record<string, unknown>>();
  await jsonFs.write(`${cfg.DATA_DIR}/config.json`, body, { pretty: true });
  ctx.json({ ok: true });
});

// ── Append to a log file ──────────────────────────────────────────────────────

app.post('/log', async (ctx) => {
  const { message } = await ctx.json<{ message: string }>();
  const line = `[${new Date().toISOString()}] ${message}\n`;
  await fs.append(`${cfg.DATA_DIR}/app.log`, line);
  ctx.json({ logged: true });
});

app.get('/log', async (ctx) => {
  const path = `${cfg.DATA_DIR}/app.log`;

  if (!await fs.exists(path)) {
    ctx.json({ lines: [] });
    return;
  }

  const text = await fs.read(path, { encoding: 'utf8' });
  const lines = text.trim().split('\n').filter(Boolean);
  ctx.json({ lines });
});

// ── File rename / delete ──────────────────────────────────────────────────────

app.delete('/upload/:name', async (ctx) => {
  const name = ctx.params.name;
  const path = `${cfg.UPLOAD_DIR}/${name}`;

  if (!await fs.exists(path)) {
    ctx.status(404).json({ error: 'not found' });
    return;
  }

  await fs.delete(path);
  ctx.json({ deleted: name });
});

app.post('/upload/:name/rename', async (ctx) => {
  const { newName } = await ctx.json<{ newName: string }>();
  const from = `${cfg.UPLOAD_DIR}/${ctx.params.name}`;
  const to   = `${cfg.UPLOAD_DIR}/${newName}`;

  await fs.rename(from, to);
  ctx.json({ ok: true, name: newName });
});

serve(app, { port: cfg.PORT });
