// LIGHTNING variant HTTP test
// Tests async batching with GPU compute kernel

import { serve, Router } from '@symmetra/http';

const app = new Router();

// Pure compute function (no memory operations)
function runLightningMachine(machineId: number): number {
    let acc = machineId;
    let result = 0;

    for (let i = 0; i < 100; i++) {
        acc = acc + 1;
        acc = acc * 2;
        acc = acc - 1;
        result = result + acc;
    }

    return result % 256;
}

function runLightningBatch(): number {
    let totalResult = 0;
    for (let m = 0; m < 32; m++) {
        totalResult = totalResult + runLightningMachine(m);
    }
    return totalResult;
}

// HTTP endpoint
app.get('/', (ctx) => {
    const result = runLightningBatch();
    ctx.json({
        success: true,
        result: result,
        timestamp: Date.now()
    });
});

// Start server on port 9000
serve(app, { port: 9000, streams: 32 });
