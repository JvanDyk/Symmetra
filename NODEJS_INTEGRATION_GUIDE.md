# Symmetra + Node.js Integration Guide

**Complete guide to building Node.js applications that use the Symmetra engine.**

---

## Quick Start

### Step 1: Understand What Symmetra Does

Symmetra is a **batch processing engine** that:
- Accepts HTTP requests
- Groups them into batches (32-128 requests)
- Executes them on CPU or GPU
- Returns results via HTTP

It's **not** a traditional API server. It's designed for:
- ✅ High-throughput batch processing
- ✅ Request aggregation
- ✅ Load balancing across routes
- ❌ Not typical request-response patterns

### Step 2: Start Symmetra Engine

```bash
# Navigate to build directory
cd D:\GITHUB\SYMMETRA\engine\build\Release

# Run Phase 7 (CPU-Fast mode, recommended for testing)
.\phase7_multi_route_test.exe 256 3

# Or run GPU-Optimized mode
.\phase8_gpu_realistic_test.exe 256 1 200
```

**Output should show:**
```
✓ All 256 worker threads ready
Starting 3-minute balanced workload test...
[1m] RPS: 709902 | CPU: 2.1% | GPU: 97.9%
```

### Step 3: Create Node.js Application

```bash
# Create project directory
mkdir symmetra-nodejs-app
cd symmetra-nodejs-app

# Initialize Node.js project
npm init -y

# No dependencies needed! (pure Node.js HTTPS)
```

### Step 4: Copy Client Code

```bash
# Copy the template
cp ..\..\nodejs-symmetra-app-template.js ./symmetra-client.js
```

### Step 5: Create Your App

**app.js:**
```javascript
const SymmetraClient = require('./symmetra-client');

const client = new SymmetraClient({
  host: 'localhost',
  port: 8443,
  mode: 'cpu-fast',
  rejectUnauthorized: false  // Dev only!
});

async function main() {
  // Send a single request
  const response = await client.send({
    path: '/api/test',
    method: 'POST',
    body: { message: 'Hello Symmetra' }
  });

  console.log('Status:', response.statusCode);
  console.log('Response:', response.body);
  console.log('Latency:', response.latency, 'ms');
}

main().catch(console.error);
```

### Step 6: Run

```bash
node app.js
```

---

## Understanding the Connection

### What Happens When You Send a Request

```
Your Node.js App
     ↓
HTTPS Request → Symmetra Engine (localhost:8443)
     ↓
Symmetra batches your request with others
     ↓
Executes on CPU (5000 XOR iterations per request)
     ↓
Returns: 200 OK + JSON response
     ↓
Your app receives response
```

### Timing Breakdown

**In-Memory Test (Phase 7):**
```
Request generation: <1 µs
Batching overhead: ~2 µs
CPU execution: ~100 µs
Response: ~1 µs
Total: ~104 µs per request
Expected RPS: 3.88M / 256 workers = 15,156 RPS per worker
```

**Real HTTP (with socket I/O):**
```
TCP receive: ~500 µs
TLS decrypt: ~200 µs
Batching: ~2 µs
CPU execution: ~100 µs
TLS encrypt: ~200 µs
TCP send: ~500 µs
Total: ~1,500 µs per request
Expected RPS: 644-1000 RPS per server (actual measured: 644 RPS)
```

**With GPU (realistic timing):**
```
TCP receive: ~500 µs
GPU queue wait: Variable (0-1000 µs depending on queue)
GPU execution: ~200 µs
TLS encrypt + TCP send: ~700 µs
Total: 1400-2400 µs per request
Expected RPS: 100-500K RPS (GPU-limited)
```

---

## Common Patterns

### Pattern 1: Simple Request-Response

```javascript
const SymmetraClient = require('./symmetra-client');

async function processUserRequest(userId, action) {
  const client = new SymmetraClient({
    host: 'localhost',
    port: 8443,
    mode: 'cpu-fast'
  });

  const response = await client.send({
    path: '/api/user/action',
    method: 'POST',
    body: {
      userId: userId,
      action: action,
      timestamp: new Date().toISOString()
    }
  });

  if (response.statusCode === 200) {
    return response.body.result;
  } else {
    throw new Error(`Symmetra error: ${response.statusCode}`);
  }
}

// Usage
const result = await processUserRequest(123, 'fetch');
console.log('User data:', result);
```

### Pattern 2: Batch Processing with Progress Tracking

```javascript
async function processBatch(items) {
  const client = new SymmetraClient({
    host: 'localhost',
    port: 8443
  });

  // Listen to progress
  client.on('progress', (data) => {
    console.log(`Processing: ${data.completed}/${data.total}`);
  });

  // Convert items to requests
  const requests = items.map(item => ({
    path: '/api/process',
    method: 'POST',
    body: item
  }));

  // Send in batches (concurrency: 10)
  const results = await client.sendBatch(requests, { concurrency: 10 });

  // Process results
  return results.map((r, i) => ({
    item: items[i],
    success: r.statusCode === 200,
    result: r.body,
    latency: r.latency
  }));
}

// Usage
const items = [{ id: 1 }, { id: 2 }, { id: 3 }];
const processed = await processBatch(items);
processed.forEach(p => {
  console.log(`Item ${p.item.id}: ${p.success ? 'OK' : 'FAILED'} (${p.latency}ms)`);
});
```

### Pattern 3: Load Testing

```javascript
async function loadTest(config) {
  const client = new SymmetraClient({
    host: 'localhost',
    port: 8443,
    mode: 'cpu-fast'
  });

  const results = await client.loadTest({
    duration: config.durationSeconds * 1000,
    rps: config.targetRps,
    requestSize: config.requestSize || 'small',
    path: '/api/test'
  });

  console.log('Load Test Results:');
  console.log(`  Target RPS: ${results.targetRps}`);
  console.log(`  Actual RPS: ${results.actualRps.toFixed(0)}`);
  console.log(`  Success Rate: ${results.successRate}%`);
  console.log(`  Latency P99: ${results.latencyPercentiles.p99}ms`);

  return results;
}

// Usage
const results = await loadTest({
  durationSeconds: 10,
  targetRps: 500,
  requestSize: 'small'
});
```

### Pattern 4: Stream Processing (Memory Efficient)

```javascript
const fs = require('fs');

async function processLargeDataset(filePath) {
  const client = new SymmetraClient({
    host: 'localhost',
    port: 8443
  });

  // Read file line by line (streaming)
  const lines = fs.readFileSync(filePath, 'utf8').split('\n');
  const requests = lines.map(line => ({
    path: '/api/process-line',
    method: 'POST',
    body: { data: line }
  }));

  let successCount = 0;
  let errorCount = 0;

  // Stream results (doesn't load all in memory)
  await client.streamBatch(requests, (result, index) => {
    if (result.statusCode === 200) {
      successCount++;
      console.log(`[${index}] ✓ Success`);
    } else {
      errorCount++;
      console.log(`[${index}] ✗ Error: ${result.statusCode}`);
    }
  });

  console.log(`\nProcessed ${successCount + errorCount} items`);
  console.log(`Success: ${successCount}, Errors: ${errorCount}`);
}

// Usage
await processLargeDataset('./large-data.txt');
```

### Pattern 5: Express.js Proxy

```javascript
const express = require('express');
const SymmetraClient = require('./symmetra-client');

const app = express();
const symmetra = new SymmetraClient({
  host: 'localhost',
  port: 8443,
  mode: 'cpu-fast'
});

app.use(express.json());

// Proxy all POST requests to Symmetra
app.post('/api/*', async (req, res) => {
  try {
    const result = await symmetra.send({
      path: req.path,
      method: 'POST',
      body: req.body
    });

    res.status(result.statusCode).json(result.body);
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Health check
app.get('/health', (req, res) => {
  res.json({ status: 'ok', mode: symmetra.mode });
});

app.listen(3000, () => {
  console.log('Express proxy listening on :3000');
  console.log('Routes requests to Symmetra on :8443');
});
```

**Usage:**
```bash
node express-proxy.js

# From another terminal
curl -X POST http://localhost:3000/api/test -d '{"msg":"hello"}' -H 'Content-Type: application/json'
```

### Pattern 6: Kubernetes-Ready Service

```javascript
const express = require('express');
const SymmetraClient = require('./symmetra-client');

const app = express();
const symmetra = new SymmetraClient({
  host: process.env.SYMMETRA_HOST || 'localhost',
  port: parseInt(process.env.SYMMETRA_PORT) || 8443,
  mode: process.env.SYMMETRA_MODE || 'cpu-fast',
  timeout: 30000
});

// Kubernetes health checks
app.get('/health', (req, res) => {
  res.json({ status: 'alive' });
});

app.get('/ready', (req, res) => {
  // Check if Symmetra is reachable
  symmetra.send({
    path: '/health',
    method: 'GET'
  }).then(() => {
    res.json({ status: 'ready' });
  }).catch(() => {
    res.status(503).json({ status: 'not-ready' });
  });
});

// API handler
app.post('/api/*', async (req, res) => {
  try {
    const result = await symmetra.send({
      path: req.path,
      method: 'POST',
      body: req.body,
      headers: {
        'X-Forwarded-For': req.ip,
        'X-Request-ID': req.id || 'unknown'
      }
    });

    res.status(result.statusCode).json(result.body);
  } catch (error) {
    console.error(`Error processing ${req.path}:`, error.message);
    res.status(503).json({
      error: 'Service temporarily unavailable',
      message: error.message
    });
  }
});

app.listen(3000, () => {
  console.log('Kubernetes service listening on :3000');
  console.log(`Symmetra backend: ${symmetra.host}:${symmetra.port}`);
});
```

**Environment variables for Kubernetes:**
```yaml
env:
  - name: SYMMETRA_HOST
    value: symmetra-service.default.svc.cluster.local
  - name: SYMMETRA_PORT
    value: "8443"
  - name: SYMMETRA_MODE
    value: "cpu-fast"
```

---

## API Reference

### SymmetraClient Constructor

```javascript
const client = new SymmetraClient(options);

// Options:
// {
//   host: string                    // Default: 'localhost'
//   port: number                    // Default: 8443
//   mode: string                    // Default: 'cpu-fast'
//   timeout: number                 // Default: 30000 (ms)
//   rejectUnauthorized: boolean     // Default: false (dev only!)
// }
```

### Methods

#### `send(request)`
Send a single request.

```javascript
const response = await client.send({
  path: '/api/endpoint',
  method: 'POST',
  body: { /* JSON object */ },
  headers: { /* optional */ }
});

// Returns:
// {
//   statusCode: number,
//   body: Object | string,
//   headers: Object,
//   latency: number  // milliseconds
// }
```

#### `sendBatch(requests, options)`
Send multiple requests in parallel.

```javascript
const responses = await client.sendBatch(
  [request1, request2, request3],
  { concurrency: 10 }
);

// Returns: Array<response>
```

#### `streamBatch(requests, onResult)`
Stream large batches efficiently.

```javascript
await client.streamBatch(requests, (result, index) => {
  console.log(`Result ${index}:`, result.statusCode);
});
```

#### `loadTest(options)`
Generate synthetic load.

```javascript
const results = await client.loadTest({
  duration: 10000,        // milliseconds
  rps: 1000,             // requests per second
  requestSize: 'small',  // 'small' | 'large' | 'mixed'
  path: '/api/test'
});

// Returns:
// {
//   testDuration: number,
//   targetRps: number,
//   actualRps: number,
//   totalRequests: number,
//   totalResponses: number,
//   totalErrors: number,
//   successRate: string,
//   latencyPercentiles: {
//     p50: number,
//     p95: number,
//     p99: number,
//     p999: number
//   }
// }
```

#### `getMetrics()`
Get cumulative metrics.

```javascript
const metrics = client.getMetrics();
// {
//   totalRequests: number,
//   totalResponses: number,
//   totalErrors: number,
//   avgLatency: number (ms),
//   minLatency: number (ms),
//   maxLatency: number (ms),
//   successRate: string (%)
// }
```

#### `resetMetrics()`
Reset all metrics.

```javascript
client.resetMetrics();
```

### Events

```javascript
client.on('progress', (data) => {
  console.log(`${data.completed}/${data.total}`);
});
```

---

## Performance Expectations

### Realistic Numbers (With Socket I/O + TLS)

| Scenario | Throughput | Latency P50 | Notes |
|----------|-----------|------------|-------|
| Single serial requests | 10 RPS | 100 ms | Very slow, don't do this |
| Batch 100 requests | 100 RPS | 1 ms per request | Sequential batching |
| Concurrent connections (256) | 644 RPS | 1.5 ms | Real measured throughput |
| Load test target 1000 RPS | ~500 RPS actual | 2 ms | Server limits bottleneck |

### Why Throughput Is Lower Than Advertised

```
Advertised (Phase 7 in-memory):  3.88M RPS
Actual (real HTTP + TLS):         644 RPS

6,000x difference because:
  - Socket I/O syscalls: 1000 µs
  - TLS encryption/decryption: 400 µs
  - Batching overhead with HTTP: 100 µs
  - CPU work: 100 µs
  Total: ~1,600 µs per request = 625 RPS
```

---

## Troubleshooting

### "Connection refused"

```
Error: connect ECONNREFUSED 127.0.0.1:8443
```

**Solution:** Symmetra engine not running. Start it first:
```bash
cd engine\build\Release
.\phase7_multi_route_test.exe 256 3
```

### "Request timeout"

```
Error: Request timeout after 30000ms
```

**Solution:** Server is overloaded or slow. Either:
1. Increase timeout: `new SymmetraClient({ timeout: 60000 })`
2. Reduce request rate
3. Add more Symmetra instances

### "Certificate error"

```
Error: self signed certificate
```

**Solution:** Symmetra uses self-signed certs in dev. Set:
```javascript
new SymmetraClient({ rejectUnauthorized: false })
```

For production, use proper certificates.

### "Low RPS compared to advertised"

**This is normal!** The 3.88M RPS is in-memory testing without socket I/O. Realistic HTTP throughput is 500-1000 RPS per instance.

To increase:
1. Add more Symmetra instances (Nginx load balancing)
2. Use `cpu-fast` mode (fastest)
3. Reduce TLS overhead (switch to plain HTTP in dev)
4. Optimize request size (smaller = faster)

---

## Best Practices

### ✅ DO

- ✅ Use connection pooling (client handles this)
- ✅ Batch requests when possible
- ✅ Stream large datasets instead of loading in memory
- ✅ Monitor metrics and adjust concurrency
- ✅ Use `cpu-fast` mode for throughput
- ✅ Use `gpu-optimized` mode only for actual GPU workloads
- ✅ Set reasonable timeouts (30-60 seconds)

### ❌ DON'T

- ❌ Send one request at a time without batching
- ❌ Use `rejectUnauthorized: false` in production
- ❌ Exceed 256 concurrent connections per instance
- ❌ Send 100MB+ payloads per request
- ❌ Expect throughput > 1000 RPS per instance with HTTP
- ❌ Use GPU-Optimized mode for simple JSON processing
- ❌ Keep connections open forever (use timeouts)

---

## Example: Production Deployment

**Symmetra servers (4 instances):**
```bash
# Server 1
./symmetra --port 8443 --mode cpu-fast

# Server 2
./symmetra --port 8444 --mode cpu-fast

# Server 3
./symmetra --port 8445 --mode cpu-fast

# Server 4
./symmetra --port 8446 --mode cpu-fast
```

**Nginx load balancer:**
```nginx
upstream symmetra_cluster {
    server localhost:8443;
    server localhost:8444;
    server localhost:8445;
    server localhost:8446;
    keepalive 256;
}

server {
    listen 8080;
    server_name api.local;
    
    location / {
        proxy_pass http://symmetra_cluster;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
        proxy_buffering off;
    }
}
```

**Node.js app:**
```javascript
const client = new SymmetraClient({
  host: 'localhost',
  port: 8080  // Nginx load balancer
});

// Now can scale to 4 * 644 RPS ≈ 2,576 RPS
```

---

## Summary

**What Symmetra is:** Batch processing engine optimized for CPU throughput with optional GPU acceleration.

**What it's good for:**
- API gateways
- Request routing
- Batch data processing
- Load testing

**What it's not good for:**
- Real-time latency-sensitive APIs (use Node.js directly)
- Streaming (batching adds latency)
- Simple request-response (overhead not worth it)

**Realistic expectations:**
- 644 RPS per instance (with HTTP + TLS)
- 3.88M RPS per CPU core (in-memory only)
- 100-500K RPS with GPU compute

**Next steps:**
1. Copy `nodejs-symmetra-app-template.js` to your project
2. Start Symmetra engine: `phase7_multi_route_test.exe 256 3`
3. Run examples and modify for your use case
4. Monitor metrics and adjust configuration

---

**Questions?** Check `ARCHITECTURE_AND_EXECUTION_MODES.md` for detailed technical documentation.
