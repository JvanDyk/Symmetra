# Quick Start: Symmetra + Node.js

**TL;DR: Get running in 5 minutes**

---

## Step 1: Start Symmetra Engine

```bash
cd D:\GITHUB\SYMMETRA\engine\build\Release
.\phase7_multi_route_test.exe 256 3
```

**Expected output:**
```
✓ All 256 worker threads ready
Starting 3-minute balanced workload test...
[1m] RPS: 709902 | CPU: 2.1% | GPU: 97.9%
```

Engine is now listening on **localhost:8443**

---

## Step 2: Create Node.js App

**app.js:**
```javascript
const https = require('https');

async function sendRequest(body) {
  return new Promise((resolve, reject) => {
    const options = {
      hostname: 'localhost',
      port: 8443,
      path: '/api/test',
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Content-Length': Buffer.byteLength(JSON.stringify(body))
      },
      rejectUnauthorized: false  // Dev only!
    };

    const req = https.request(options, (res) => {
      let data = '';
      res.on('data', chunk => data += chunk);
      res.on('end', () => {
        resolve({
          statusCode: res.statusCode,
          body: data ? JSON.parse(data) : {},
          latency: Date.now() - startTime
        });
      });
    });

    req.on('error', reject);
    const startTime = Date.now();
    req.write(JSON.stringify(body));
    req.end();
  });
}

// Test it
(async () => {
  const result = await sendRequest({ message: 'Hello Symmetra' });
  console.log('Status:', result.statusCode);
  console.log('Body:', result.body);
  console.log('Latency:', result.latency, 'ms');
})();
```

**Run it:**
```bash
node app.js
```

---

## Step 3: Use the Template Client (Better)

**Copy the template:**
```bash
cp ..\..\nodejs-symmetra-app-template.js ./client.js
```

**Use it:**
```javascript
const SymmetraClient = require('./client');

const client = new SymmetraClient({
  host: 'localhost',
  port: 8443,
  mode: 'cpu-fast',
  rejectUnauthorized: false
});

(async () => {
  // Single request
  const response = await client.send({
    path: '/api/test',
    method: 'POST',
    body: { msg: 'hello' }
  });
  console.log('Response:', response.statusCode, response.body);

  // Batch 100 requests
  const requests = Array(100).fill(null).map((_, i) => ({
    path: '/api/data',
    body: { id: i }
  }));
  const results = await client.sendBatch(requests, { concurrency: 10 });
  console.log('Sent 100, got', results.filter(r => r.statusCode === 200).length, 'responses');

  // Load test (5 seconds at 500 RPS target)
  const loadResults = await client.loadTest({
    duration: 5000,
    rps: 500,
    requestSize: 'small'
  });
  console.log('Actual RPS:', loadResults.actualRps.toFixed(0));
})();
```

---

## Performance Expectations

```
Single instance (CPU-Fast mode):
  Throughput: 644 RPS with real HTTP + TLS
  Latency p50: 1.5 ms
  Latency p99: 10 ms

What you'll actually measure:
  Depends on:
    - How many concurrent requests
    - Request size
    - Server load
    - Your network latency
```

---

## What Each Mode Does

| Mode | Throughput | Good For | Code |
|------|-----------|----------|------|
| **CPU-Fast** | 644 RPS | API routing, caching | `./phase7_multi_route_test.exe` |
| **GPU-Optimized** | 100-500K | ML inference, image processing | `./phase8_gpu_realistic_test.exe` |
| **Balanced** | 614K | ❌ Not recommended (architectural limit) | Don't use |

---

## Common Patterns

### Pattern 1: Single Request
```javascript
const response = await client.send({
  path: '/api/user/123',
  method: 'POST',
  body: { action: 'fetch' }
});
console.log(response.statusCode, response.body);
```

### Pattern 2: Batch Process
```javascript
const requests = items.map(item => ({
  path: '/api/process',
  body: item
}));
const results = await client.sendBatch(requests, { concurrency: 10 });
```

### Pattern 3: Load Test
```javascript
const results = await client.loadTest({
  duration: 10000,  // 10 seconds
  rps: 1000,        // Target 1000 RPS
  requestSize: 'small'
});
console.log('Actual RPS:', results.actualRps.toFixed(0));
console.log('P99 latency:', results.latencyPercentiles.p99, 'ms');
```

### Pattern 4: Stream Large Batches
```javascript
let count = 0;
await client.streamBatch(requests, (result, index) => {
  if (result.statusCode === 200) count++;
  if ((index + 1) % 100 === 0) console.log(`Processed: ${index + 1}`);
});
console.log('Success:', count);
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `ECONNREFUSED` | Start Symmetra: `.\phase7_multi_route_test.exe 256 3` |
| `self signed certificate` | Set `rejectUnauthorized: false` in client options |
| Low RPS (< 10) | Normal. You're hitting network overhead. Try load test with `concurrency: 10` |
| Timeouts | Server overloaded. Try `{ timeout: 60000 }` or reduce request rate |
| Memory usage grows | Stream instead of batch: `streamBatch(requests, callback)` |

---

## API Cheat Sheet

```javascript
// Create client
const client = new SymmetraClient(options);

// Send one request
await client.send(request);

// Send many in parallel
await client.sendBatch(requests, { concurrency: 10 });

// Stream processing (memory efficient)
await client.streamBatch(requests, (result, index) => {});

// Load test
await client.loadTest({ duration, rps, requestSize, path });

// Get metrics
client.getMetrics();  // { totalRequests, totalResponses, avgLatency, ... }

// Reset metrics
client.resetMetrics();

// Listen to events
client.on('progress', (data) => {
  console.log(`${data.completed}/${data.total}`);
});
```

---

## Production Checklist

- ✅ Set `rejectUnauthorized: true` with proper TLS certs
- ✅ Use connection pooling (client handles this)
- ✅ Monitor metrics with `getMetrics()`
- ✅ Set reasonable timeout (30-60 seconds)
- ✅ Use batch/stream, not single requests
- ✅ Load balance across multiple instances if needed
- ✅ Use `cpu-fast` mode for API throughput
- ✅ Use separate GPU service if you need GPU compute

---

## Next Steps

1. **For detailed architecture:** Read `ARCHITECTURE_AND_EXECUTION_MODES.md`
2. **For implementation patterns:** Read `NODEJS_INTEGRATION_GUIDE.md`
3. **For all methods/options:** See `nodejs-symmetra-app-template.js` source

---

**You're ready! Start the engine and run a test.** 🚀
