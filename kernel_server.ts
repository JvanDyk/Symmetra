// HTTP server that loads and executes the compiled Turing kernel
import { serve, Router } from '@symmetra/http';

const app = new Router();

// Endpoint that runs the Turing machine kernel 32 times (batched warp simulation)
app.get('/bench/turing', (ctx) => {
    let totalOnes = 0;
    let totalSteps = 0;

    // Simulate 32 parallel machines (one warp)
    for (let w = 0; w < 32; w++) {
        // Each machine runs the same computation
        const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

        let state = 0;
        let pos = 20;
        let steps = 0;
        const maxSteps = 500;

        while (steps < maxSteps && state !== -1) {
            if (pos < 0 || pos >= 40) {
                state = -1;
                break;
            }

            const symbol = tape[pos];

            if (state === 0) {
                if (symbol === 0) {
                    tape[pos] = 1;
                    pos = pos + 1;
                    state = 1;
                } else {
                    tape[pos] = 0;
                    pos = pos - 1;
                    state = 2;
                }
            } else if (state === 1) {
                if (symbol === 0) {
                    tape[pos] = 1;
                    pos = pos + 1;
                    state = 0;
                } else {
                    tape[pos] = 1;
                    pos = pos - 1;
                    state = 0;
                }
            } else if (state === 2) {
                if (symbol === 0) {
                    tape[pos] = 0;
                    pos = pos + 1;
                    state = 1;
                } else {
                    tape[pos] = 0;
                    pos = pos - 1;
                    state = 1;
                }
            }

            steps = steps + 1;
        }

        let ones = 0;
        for (let i = 0; i < 40; i++) {
            if (tape[i] === 1) {
                ones = ones + 1;
            }
        }

        totalOnes = totalOnes + ones;
        totalSteps = totalSteps + steps;
    }

    ctx.json({
        benchmark: "Turing Machine 3-state (32 parallel instances)",
        warmBatches: 1,
        machinesPerBatch: 32,
        tapeSize: 40,
        maxStepsPerMachine: 500,
        totalStepsExecuted: totalSteps,
        totalOnesGenerated: totalOnes,
        avgStepsPerMachine: totalSteps / 32,
        avgOnesPerMachine: totalOnes / 32,
        note: "All 32 machines execute identical code path on same CPU core (simulating warp coherence)"
    });
});

// Health check
app.get('/health', (ctx) => {
    ctx.text("OK");
});

serve(app, { port: 8080 });
