import { serve, Router } from '@symmetra/http';

const app = new Router();

// Ultra-simple Turing machine: 3 states, run N steps
// No stdlib — just raw logic
app.get('/tm-simple', (ctx) => {
    // Batch 32 machines (like 1 warp)
    // All execute identical code path

    let totalSteps = 0;
    let totalOnes = 0;

    for (let machineId = 0; machineId < 32; machineId++) {
        // Single tape array per machine
        const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

        let state = 0;
        let pos = 20;  // Center
        let steps = 0;
        const maxSteps = 500;

        // Run state machine
        while (steps < maxSteps && state !== -1) {
            if (pos < 0 || pos >= 40) {
                state = -1;  // Halt at boundary
                break;
            }

            const symbol = tape[pos];

            // Simple 3-state machine
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

        // Count ones on tape
        let ones = 0;
        for (let i = 0; i < 40; i++) {
            if (tape[i] === 1) {
                ones = ones + 1;
            }
        }

        totalSteps = totalSteps + steps;
        totalOnes = totalOnes + ones;
    }

    ctx.json({
        benchmark: "Turing Machine (3-state, minimal)",
        machineCount: 32,
        tapeSize: 40,
        maxStepsPerMachine: 500,
        totalStepsExecuted: totalSteps,
        avgStepsPerMachine: totalSteps / 32,
        totalOnesGenerated: totalOnes,
        note: "All 32 machines execute identical code path (perfect warp coherence)"
    });
});

serve(app, { port: 8080 });
