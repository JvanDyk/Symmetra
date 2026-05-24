/**
 * Symmetra Filesystem Example
 * Demonstrates: readFile, writeFile, appendFile, stat, readDir, file uploads.
 * Run: sym ./server.ts
 */

import { serve, Router } from '@symmetra/http';
import { readFile, writeFile, appendFile, stat, exists, readDir, mkdir } from '@symmetra/fs';

const app  = new Router();
const BASE = './uploads';

// Ensure upload directory exists on startup
await mkdir(BASE, { recursive: true });

// ── Static file serving ───────────────────────────────────────────────────────

app.get('/files', async (ctx) => {
  const entries = await readDir(BASE);
  const files   = entries.filter(e => e.isFile).map(e => ({
    name:     e.name,
    size:     e.size,
    sizeKb:   (e.size / 1024).toFixed(1),
  }));
  ctx.json({ count: files.length, files });
});

app.get('/files/:name', async (ctx) => {
  const path = `${BASE}/${ctx.params.name}`;

  if (!await exists(path)) {
    ctx.status(404).json({ error: 'file not found' });
    return;
  }

  const info = await stat(path);
  if (info.isDirectory) {
    ctx.status(400).json({ error: 'path is a directory' });
    return;
  }

  const content = await readFile(path, 'utf8');
  ctx.text(content, guessContentType(ctx.params.name));
});

// ── File upload ───────────────────────────────────────────────────────────────

app.put('/files/:name', async (ctx) => {
  const name = ctx.params.name;

  // Basic path traversal guard
  if (name.includes('/') || name.includes('..')) {
    ctx.status(400).json({ error: 'invalid filename' });
    return;
  }

  const path    = `${BASE}/${name}`;
  const content = await ctx.text();
  await writeFile(path, content);

  const info = await stat(path);
  ctx.status(201).json({ ok: true, name, size: info.size });
});

// ── Append to file ────────────────────────────────────────────────────────────

app.post('/files/:name/append', async (ctx) => {
  const path    = `${BASE}/${ctx.params.name}`;
  const content = await ctx.text();
  await appendFile(path, content);
  ctx.json({ ok: true });
});

// ── File metadata ─────────────────────────────────────────────────────────────

app.get('/files/:name/stat', async (ctx) => {
  const path = `${BASE}/${ctx.params.name}`;
  if (!await exists(path)) {
    ctx.status(404).json({ error: 'not found' });
    return;
  }
  const info = await stat(path);
  ctx.json({
    name:    ctx.params.name,
    size:    info.size,
    isFile:  info.isFile,
    mtime:   info.mtime,
  });
});

// ── Log appender demo ─────────────────────────────────────────────────────────

app.post('/log', async (ctx) => {
  const { message } = await ctx.json<{ message: string }>();
  const line = `[${new Date(Date.now()).toISOString()}] ${message}\n`;
  await appendFile(`${BASE}/server.log`, line);
  ctx.json({ logged: true });
});

app.get('/log', async (ctx) => {
  const path = `${BASE}/server.log`;
  if (!await exists(path)) {
    ctx.json({ lines: [] });
    return;
  }
  const text  = await readFile(path, 'utf8');
  const lines = text.trim().split('\n').filter(l => l.length > 0);
  ctx.json({ count: lines.length, lines: lines.slice(-50) });
});

// ── Helpers ───────────────────────────────────────────────────────────────────

function guessContentType(name: string): string {
  if (name.endsWith('.json'))  return 'application/json';
  if (name.endsWith('.html'))  return 'text/html';
  if (name.endsWith('.css'))   return 'text/css';
  if (name.endsWith('.js'))    return 'application/javascript';
  if (name.endsWith('.md'))    return 'text/markdown';
  return 'text/plain';
}

// ── Start ─────────────────────────────────────────────────────────────────────

serve(app, { port: 8080 });
