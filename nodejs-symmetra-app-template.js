/**
 * Symmetra Node.js Application Template
 *
 * Complete, working example of integrating with Symmetra engine
 * Demonstrates:
 * - Single request execution
 * - Batch processing
 * - Load testing
 * - Metrics collection
 * - Error handling
 *
 * Usage:
 *   node nodejs-symmetra-app-template.js
 */

const https = require('https');
const fs = require('fs');
const { EventEmitter } = require('events');

/**
 * SymmetraClient - Main client for communicating with Symmetra engine
 */
class SymmetraClient extends EventEmitter {
  constructor(options = {}) {
    super();

    this.host = options.host || 'localhost';
    this.port = options.port || 8443;
    this.mode = options.mode || 'cpu-fast';
    this.timeout = options.timeout || 30000;
    this.rejectUnauthorized = options.rejectUnauthorized || false;  // Dev only!

    // Connection pooling
    this.agent = new https.Agent({
      rejectUnauthorized: this.rejectUnauthorized,
      keepAlive: true,
      keepAliveMsecs: 30000,
      maxSockets: 256,
      maxFreeSockets: 128
    });

    // Metrics
    this.metrics = {
      totalRequests: 0,
      totalResponses: 0,
      totalErrors: 0,
      totalLatency: 0,
      minLatency: Infinity,
      maxLatency: 0
    };
  }

  /**
   * Send a single request to Symmetra
   *
   * @param {Object} request - Request object
   * @param {string} request.path - API path (e.g., '/api/compute')
   * @param {string} request.method - HTTP method (default: 'POST')
   * @param {Object|string} request.body - Request body
   * @param {Object} request.headers - Additional headers
   * @returns {Promise<Object>} Response object with statusCode, body, headers
   *
   * Example:
   *   const response = await client.send({
   *     path: '/api/data',
   *     method: 'POST',
   *     body: { userId: 123, action: 'fetch' }
   *   });
   */
  async send(request) {
    return new Promise((resolve, reject) => {
      const startTime = Date.now();

      // Serialize body if object
      const bodyStr = typeof request.body === 'string'
        ? request.body
        : JSON.stringify(request.body || {});

      const options = {
        hostname: this.host,
        port: this.port,
        path: request.path || '/api/default',
        method: request.method || 'POST',
        headers: {
          'Content-Type': 'application/json',
          'X-Mode': this.mode,
          'Content-Length': Buffer.byteLength(bodyStr),
          ...(request.headers || {})
        },
        agent: this.agent
      };

      const req = https.request(options, (res) => {
        let data = '';

        res.on('data', chunk => {
          data += chunk;
        });

        res.on('end', () => {
          const latency = Date.now() - startTime;

          // Update metrics
          this.metrics.totalResponses++;
          this.metrics.totalLatency += latency;
          this.metrics.minLatency = Math.min(this.metrics.minLatency, latency);
          this.metrics.maxLatency = Math.max(this.metrics.maxLatency, latency);

          try {
            const body = data ? JSON.parse(data) : {};
            resolve({
              statusCode: res.statusCode,
              body: body,
              headers: res.headers,
              latency: latency
            });
          } catch (e) {
            resolve({
              statusCode: res.statusCode,
              body: data,
              headers: res.headers,
              latency: latency
            });
          }
        });
      });

      req.on('error', (error) => {
        this.metrics.totalErrors++;
        reject(error);
      });

      req.setTimeout(this.timeout, () => {
        this.metrics.totalErrors++;
        req.destroy();
        reject(new Error(`Request timeout after ${this.timeout}ms`));
      });

      this.metrics.totalRequests++;
      req.write(bodyStr);
      req.end();
    });
  }

  /**
   * Send multiple requests in parallel
   *
   * @param {Array<Object>} requests - Array of request objects
   * @param {Object} options - { concurrency: 10 }
   * @returns {Promise<Array>} Array of responses
   *
   * Example:
   *   const responses = await client.sendBatch([
   *     { path: '/api/user/1', method: 'GET' },
   *     { path: '/api/user/2', method: 'GET' },
   *     { path: '/api/user/3', method: 'GET' }
   *   ], { concurrency: 3 });
   */
  async sendBatch(requests, options = {}) {
    const { concurrency = 10 } = options;
    const results = [];

    // Process in chunks to avoid overwhelming the server
    for (let i = 0; i < requests.length; i += concurrency) {
      const chunk = requests.slice(i, i + concurrency);
      const chunkResults = await Promise.all(
        chunk.map(req =>
          this.send(req).catch(err => ({
            error: err.message,
            statusCode: 0
          }))
        )
      );
      results.push(...chunkResults);

      // Emit progress
      this.emit('progress', {
        completed: Math.min(i + concurrency, requests.length),
        total: requests.length
      });
    }

    return results;
  }

  /**
   * Stream large result sets (memory efficient)
   *
   * @param {Array<Object>} requests - Request objects
   * @param {Function} onResult - Callback: (result, index) => void
   *
   * Example:
   *   await client.streamBatch(requests, (result, i) => {
   *     console.log(`Result ${i}:`, result.statusCode);
   *   });
   */
  async streamBatch(requests, onResult) {
    for (let i = 0; i < requests.length; i++) {
      const result = await this.send(requests[i]).catch(err => ({
        error: err.message,
        statusCode: 0
      }));
      onResult(result, i);

      // Emit progress every 100 requests
      if ((i + 1) % 100 === 0) {
        this.emit('progress', {
          completed: i + 1,
          total: requests.length
        });
      }
    }
  }

  /**
   * Load test - generate synthetic load and measure performance
   *
   * @param {Object} options
   * @param {number} options.duration - Test duration in milliseconds (default: 10000)
   * @param {number} options.rps - Target requests per second (default: 1000)
   * @param {string} options.requestSize - 'small' (512B) | 'large' (2KB) | 'mixed'
   * @param {string} options.path - API path (default: '/api/test')
   * @returns {Promise<Object>} Test results with throughput, latency, errors
   *
   * Example:
   *   const results = await client.loadTest({
   *     duration: 10000,
   *     rps: 1000,
   *     requestSize: 'small'
   *   });
   *   console.log(`Achieved RPS: ${results.throughput}`);
   */
  async loadTest(options = {}) {
    const {
      duration = 10000,
      rps = 1000,
      requestSize = 'small',
      path = '/api/test'
    } = options;

    console.log(`\n🚀 Starting load test:`);
    console.log(`   Duration: ${duration}ms`);
    console.log(`   Target RPS: ${rps}`);
    console.log(`   Request size: ${requestSize}`);
    console.log(`   Endpoint: ${path}\n`);

    const results = {
      testDuration: duration,
      targetRps: rps,
      actualRps: 0,
      totalRequests: 0,
      totalResponses: 0,
      totalErrors: 0,
      latencyPercentiles: {},
      latencies: []
    };

    const startTime = Date.now();
    const testStart = this.metrics.totalRequests;
    let lastReportTime = startTime;

    while (Date.now() - startTime < duration) {
      // Generate request
      const bodySize = requestSize === 'small' ? 512 :
                      requestSize === 'large' ? 2048 :
                      Math.random() < 0.5 ? 512 : 2048;

      const request = {
        path: path,
        method: 'POST',
        body: JSON.stringify({
          data: 'x'.repeat(bodySize),
          timestamp: Date.now(),
          testId: Math.random().toString(36)
        })
      };

      // Send request (don't wait for it)
      this.send(request)
        .then(response => {
          results.latencies.push(response.latency);
          if (response.statusCode === 200) {
            results.totalResponses++;
          }
        })
        .catch(err => {
          results.totalErrors++;
        });

      results.totalRequests++;

      // Report progress every 5 seconds
      const now = Date.now();
      if (now - lastReportTime >= 5000) {
        const elapsed = (now - startTime) / 1000;
        const rpsNow = results.totalRequests / elapsed;
        console.log(`   [${elapsed.toFixed(1)}s] Requests: ${results.totalRequests} | RPS: ${rpsNow.toFixed(0)}`);
        lastReportTime = now;
      }

      // Rate limiting: maintain target RPS
      const elapsed = (Date.now() - startTime) / 1000;
      const expectedRequests = rps * elapsed;
      if (results.totalRequests > expectedRequests) {
        await this.sleep(1);  // Back off slightly
      }
    }

    // Wait for in-flight requests to complete (with timeout)
    await this.sleep(2000);

    // Calculate percentiles
    const sorted = results.latencies.sort((a, b) => a - b);
    results.latencyPercentiles = {
      p50: sorted[Math.floor(sorted.length * 0.50)],
      p95: sorted[Math.floor(sorted.length * 0.95)],
      p99: sorted[Math.floor(sorted.length * 0.99)],
      p999: sorted[Math.floor(sorted.length * 0.999)]
    };

    results.actualRps = (results.totalRequests * 1000) / (Date.now() - startTime);
    results.successRate = (results.totalResponses / results.totalRequests * 100).toFixed(1);

    return results;
  }

  /**
   * Get current metrics
   */
  getMetrics() {
    return {
      ...this.metrics,
      avgLatency: this.metrics.totalResponses > 0
        ? (this.metrics.totalLatency / this.metrics.totalResponses).toFixed(2)
        : 0,
      successRate: this.metrics.totalRequests > 0
        ? ((this.metrics.totalResponses / this.metrics.totalRequests) * 100).toFixed(2)
        : 0
    };
  }

  /**
   * Reset metrics
   */
  resetMetrics() {
    this.metrics = {
      totalRequests: 0,
      totalResponses: 0,
      totalErrors: 0,
      totalLatency: 0,
      minLatency: Infinity,
      maxLatency: 0
    };
  }

  /**
   * Helper: sleep
   */
  sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }
}

/**
 * Example 1: Single request
 */
async function example1_singleRequest() {
  console.log('\n=== Example 1: Single Request ===\n');

  const client = new SymmetraClient({
    host: 'localhost',
    port: 8443,
    mode: 'cpu-fast'
  });

  try {
    const response = await client.send({
      path: '/api/test',
      method: 'POST',
      body: {
        message: 'Hello Symmetra',
        timestamp: new Date().toISOString()
      }
    });

    console.log('Response status:', response.statusCode);
    console.log('Response body:', response.body);
    console.log('Latency:', response.latency, 'ms');
  } catch (error) {
    console.error('Error:', error.message);
  }
}

/**
 * Example 2: Batch processing
 */
async function example2_batchProcessing() {
  console.log('\n=== Example 2: Batch Processing ===\n');

  const client = new SymmetraClient({
    host: 'localhost',
    port: 8443,
    mode: 'cpu-fast'
  });

  // Generate 100 test requests
  const requests = Array.from({ length: 100 }, (_, i) => ({
    path: '/api/data',
    method: 'POST',
    body: {
      id: i,
      action: 'process',
      data: 'x'.repeat(512)  // 512 byte payload
    }
  }));

  try {
    console.log('Sending 100 requests...');
    const responses = await client.sendBatch(requests, { concurrency: 10 });

    const successful = responses.filter(r => r.statusCode === 200).length;
    console.log(`\nResults:`);
    console.log(`  Total: ${responses.length}`);
    console.log(`  Successful: ${successful}`);
    console.log(`  Failed: ${responses.length - successful}`);

    // Sample latencies
    const latencies = responses
      .filter(r => r.latency)
      .map(r => r.latency)
      .sort((a, b) => a - b);

    console.log(`\nLatency stats (ms):`);
    console.log(`  Min: ${latencies[0]}`);
    console.log(`  P50: ${latencies[Math.floor(latencies.length * 0.5)]}`);
    console.log(`  P99: ${latencies[Math.floor(latencies.length * 0.99)]}`);
    console.log(`  Max: ${latencies[latencies.length - 1]}`);
  } catch (error) {
    console.error('Error:', error.message);
  }
}

/**
 * Example 3: Load testing
 */
async function example3_loadTest() {
  console.log('\n=== Example 3: Load Testing ===\n');

  const client = new SymmetraClient({
    host: 'localhost',
    port: 8443,
    mode: 'cpu-fast'
  });

  try {
    // Listen to progress events
    client.on('progress', (data) => {
      console.log(`  Progress: ${data.completed}/${data.total}`);
    });

    const results = await client.loadTest({
      duration: 10000,        // 10 seconds
      rps: 500,              // Target 500 RPS
      requestSize: 'small',  // Small requests (512B)
      path: '/api/test'
    });

    console.log('\n📊 Load Test Results:');
    console.log(`  Target RPS: ${results.targetRps}`);
    console.log(`  Actual RPS: ${results.actualRps.toFixed(0)}`);
    console.log(`  Total Requests: ${results.totalRequests}`);
    console.log(`  Successful: ${results.totalResponses}`);
    console.log(`  Failed: ${results.totalErrors}`);
    console.log(`  Success Rate: ${results.successRate}%`);
    console.log(`\n  Latency (ms):`);
    console.log(`    P50: ${results.latencyPercentiles.p50}`);
    console.log(`    P95: ${results.latencyPercentiles.p95}`);
    console.log(`    P99: ${results.latencyPercentiles.p99}`);
    console.log(`    P999: ${results.latencyPercentiles.p999}`);
  } catch (error) {
    console.error('Error:', error.message);
  }
}

/**
 * Example 4: Stream processing large datasets
 */
async function example4_streamProcessing() {
  console.log('\n=== Example 4: Stream Processing ===\n');

  const client = new SymmetraClient({
    host: 'localhost',
    port: 8443,
    mode: 'cpu-fast'
  });

  // Generate 1000 requests (memory efficient with streaming)
  const requests = Array.from({ length: 1000 }, (_, i) => ({
    path: '/api/stream',
    method: 'POST',
    body: {
      id: i,
      data: `item_${i}`
    }
  }));

  let successful = 0;
  let failed = 0;

  try {
    console.log('Streaming 1000 requests...');

    await client.streamBatch(requests, (result, index) => {
      if (result.statusCode === 200) {
        successful++;
      } else {
        failed++;
      }

      // Log every 100 requests
      if ((index + 1) % 100 === 0) {
        console.log(`  Processed: ${index + 1}/1000`);
      }
    });

    console.log(`\n✅ Streaming complete!`);
    console.log(`  Successful: ${successful}`);
    console.log(`  Failed: ${failed}`);
    console.log(`\nClient metrics:`, client.getMetrics());
  } catch (error) {
    console.error('Error:', error.message);
  }
}

/**
 * Main: Run all examples
 */
async function main() {
  console.log('╔════════════════════════════════════════════════════════════╗');
  console.log('║  Symmetra Node.js Client - Examples                        ║');
  console.log('║  Make sure Symmetra engine is running on localhost:8443    ║');
  console.log('╚════════════════════════════════════════════════════════════╝');

  // Uncomment to run examples

  // Example 1: Single request
  // await example1_singleRequest();

  // Example 2: Batch processing
  // await example2_batchProcessing();

  // Example 3: Load testing (recommended to test with real engine)
  // await example3_loadTest();

  // Example 4: Stream processing
  // await example4_streamProcessing();

  console.log('\n💡 To use these examples:');
  console.log('   1. Start Symmetra engine: ./phase7_multi_route_test.exe 256 1');
  console.log('   2. Uncomment an example function in main()');
  console.log('   3. Run: node nodejs-symmetra-app-template.js\n');
}

// Export for use as module
module.exports = SymmetraClient;

// Run examples if executed directly
if (require.main === module) {
  main().catch(console.error);
}
