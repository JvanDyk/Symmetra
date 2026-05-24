/**
 * Symmetra HTTP Client Example
 * Demonstrates: outbound fetch, parallel requests, timeout/abort, webhooks.
 * Run: sym ./server.ts
 */

import { serve, Router } from '@symmetra/http';
import { fetch } from '@symmetra/net';

const app = new Router();

// ── Simple proxy ──────────────────────────────────────────────────────────────

// GET /proxy?url=<encoded-url>
app.get('/proxy', async (ctx) => {
  const target = ctx.query.url;
  if (!target) { ctx.status(400).json({ error: 'url required' }); return; }

  const resp = await fetch(target, { timeoutMs: 10_000 });
  if (!resp.ok) {
    ctx.status(502).json({ error: `upstream returned ${resp.status}` });
    return;
  }

  const body = await resp.text();
  ctx.text(body, resp.headers.get('content-type') ?? 'text/plain');
});

// ── GitHub API aggregator ─────────────────────────────────────────────────────

app.get('/github/:owner/:repo', async (ctx) => {
  const { owner, repo } = ctx.params;
  const base = 'https://api.github.com';
  const headers = {
    'Accept': 'application/vnd.github.v3+json',
    'User-Agent': 'Symmetra-Example/1.0',
  };

  // Fetch repo info and latest release in parallel
  const [repoResp, releaseResp] = await Promise.all([
    fetch(`${base}/repos/${owner}/${repo}`, { headers }),
    fetch(`${base}/repos/${owner}/${repo}/releases/latest`, { headers }),
  ]);

  if (!repoResp.ok) {
    ctx.status(repoResp.status === 404 ? 404 : 502).json({ error: 'repo not found' });
    return;
  }

  const repoData = await repoResp.json<{
    full_name: string;
    description: string;
    stargazers_count: number;
    forks_count: number;
    open_issues_count: number;
    language: string;
  }>();

  const latestRelease = releaseResp.ok
    ? await releaseResp.json<{ tag_name: string; published_at: string }>()
    : null;

  ctx.json({
    name:         repoData.full_name,
    description:  repoData.description,
    stars:        repoData.stargazers_count,
    forks:        repoData.forks_count,
    issues:       repoData.open_issues_count,
    language:     repoData.language,
    latestRelease: latestRelease?.tag_name ?? null,
    releasedAt:    latestRelease?.published_at ?? null,
  });
});

// ── Weather (wttr.in JSON API) ────────────────────────────────────────────────

app.get('/weather/:city', async (ctx) => {
  const city = encodeURIComponent(ctx.params.city);
  const resp = await fetch(`https://wttr.in/${city}?format=j1`, { timeoutMs: 5_000 });
  if (!resp.ok) {
    ctx.status(502).json({ error: 'weather service unavailable' });
    return;
  }
  const data = await resp.json<{
    current_condition: Array<{
      temp_C: string;
      temp_F: string;
      weatherDesc: Array<{ value: string }>;
      humidity: string;
      windspeedKmph: string;
    }>;
  }>();
  const current = data.current_condition[0];
  ctx.json({
    city:        ctx.params.city,
    tempC:       current.temp_C,
    tempF:       current.temp_F,
    description: current.weatherDesc[0].value,
    humidity:    current.humidity + '%',
    windKmph:    current.windspeedKmph,
  });
});

// ── Abort / timeout demo ──────────────────────────────────────────────────────

app.get('/slow-fetch', async (ctx) => {
  const controller = new AbortController();
  const timeoutMs  = parseInt(ctx.query.timeout ?? '3000', 10);

  const timer = setTimeout(() => controller.abort(), timeoutMs);

  try {
    const resp = await fetch('https://httpbin.org/delay/10', {
      signal: controller.signal,
    });
    clearTimeout(timer);
    ctx.json({ status: resp.status, timedOut: false });
  } catch {
    clearTimeout(timer);
    ctx.status(504).json({ error: 'gateway timeout', timedOutAfterMs: timeoutMs });
  }
});

// ── Webhook forwarder ─────────────────────────────────────────────────────────

app.post('/webhook/forward', async (ctx) => {
  const { target, payload } = await ctx.json<{ target: string; payload: unknown }>();

  if (!target.startsWith('https://')) {
    ctx.status(400).json({ error: 'target must be an https:// URL' });
    return;
  }

  const resp = await fetch(target, {
    method:  'POST',
    headers: { 'Content-Type': 'application/json' },
    body:    JSON.stringify(payload),
    timeoutMs: 5_000,
  });

  ctx.json({
    forwarded: true,
    target,
    upstreamStatus: resp.status,
    ok: resp.ok,
  });
});

// ── Fan-out aggregator ────────────────────────────────────────────────────────

app.post('/fanout', async (ctx) => {
  const { urls } = await ctx.json<{ urls: string[] }>();

  if (!Array.isArray(urls) || urls.length === 0) {
    ctx.status(400).json({ error: 'urls array required' });
    return;
  }
  if (urls.length > 10) {
    ctx.status(400).json({ error: 'max 10 urls per fanout' });
    return;
  }

  const results = await Promise.all(
    urls.map(async (url) => {
      try {
        const resp = await fetch(url, { timeoutMs: 5_000 });
        return { url, status: resp.status, ok: resp.ok };
      } catch (e) {
        return { url, status: 0, ok: false, error: String(e) };
      }
    }),
  );

  ctx.json({ count: results.length, results });
});

// ── Start ─────────────────────────────────────────────────────────────────────

serve(app, { port: 8080 });
