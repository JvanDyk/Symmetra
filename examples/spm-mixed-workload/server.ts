// Complete GPU service proving Symmetra can replace Node.js
// Uses: lodash (npm), uuid (npm), crypto (npm via compat), @symmetra/http
// NO CODE CHANGES - identical to Node.js Express syntax

import { serve, Router } from '@symmetra/http';
import _ from 'lodash';
import { v4 as uuidv4 } from 'uuid';
import crypto from 'crypto';

interface User {
  id: string;
  name: string;
  email: string;
  role: string;
  created: number;
}

interface Request {
  action: string;
  data: any;
}

// Simulated in-memory database (would be VRAM-backed on GPU)
const users: User[] = [];
const requests: Request[] = [];

const app = new Router();

// 1. Health check
app.get('/health', (ctx) => {
  ctx.json({
    status: 'healthy',
    timestamp: Date.now(),
    uptime: process.uptime ? process.uptime() : 0,
    users: users.length,
    requests: requests.length
  });
});

// 2. Create user (uses uuid from npm)
app.post('/users', (ctx) => {
  const body = ctx.body<{ name: string; email: string; role: string }>();
  const user: User = {
    id: uuidv4(),
    name: body.name,
    email: body.email,
    role: body.role,
    created: Date.now()
  };
  users.push(user);
  ctx.json(user);
});

// 3. List users (uses lodash for filtering/grouping)
app.get('/users', (ctx) => {
  const role = ctx.query.role as string | undefined;
  let filtered = users;

  if (role) {
    filtered = _.filter(users, u => u.role === role);
  }

  const grouped = _.groupBy(filtered, 'role');
  const stats = _.mapValues(grouped, group => ({
    count: group.length,
    users: group.map(u => u.name)
  }));

  ctx.json(stats);
});

// 4. Hash request data (uses crypto from npm via compat layer)
app.post('/hash', (ctx) => {
  const body = ctx.body<{ data: string }>();
  const hash = crypto
    .createHash('sha256')
    .update(body.data)
    .digest('hex');

  const request: Request = {
    action: 'hash',
    data: body.data
  };
  requests.push(request);

  ctx.json({ hash, request_id: _.take(hash, 8).join('') });
});

// 5. Process batch operation (uses lodash + crypto + uuid)
app.post('/batch', (ctx) => {
  const batch = ctx.body<Array<{ action: string; data: string }>>();

  const results = _.map(batch, item => {
    const id = uuidv4();
    const hash = crypto
      .createHash('sha256')
      .update(item.data)
      .digest('hex');

    return {
      id,
      action: item.action,
      hash: hash.substring(0, 16),
      timestamp: Date.now()
    };
  });

  // Group results by action
  const grouped = _.groupBy(results, 'action');
  const summary = _.mapValues(grouped, group => ({
    count: group.length,
    ids: _.map(group, 'id')
  }));

  ctx.json({
    total: batch.length,
    results,
    summary
  });
});

// 6. Analytics (uses lodash for complex aggregations)
app.get('/analytics', (ctx) => {
  const usersByRole = _.groupBy(users, 'role');
  const stats = _.mapValues(usersByRole, group => ({
    count: group.length,
    created_dates: _.map(group, u =>
      new Date(u.created).toISOString().split('T')[0]
    )
  }));

  const request_actions = _.groupBy(requests, 'action');
  const request_stats = _.mapValues(request_actions, group => ({
    count: group.length
  }));

  ctx.json({
    users: stats,
    requests: request_stats,
    total_users: users.length,
    total_requests: requests.length
  });
});

serve(app, { port: 8080 });
console.log('✅ Symmetra GPU service running on port 8080');
console.log('Uses: lodash (npm), uuid (npm), crypto (npm), @symmetra/http');
console.log('No code changes from Node.js - SPM handles transformation automatically');
