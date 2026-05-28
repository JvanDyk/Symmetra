// Pure Turing machine kernel for GPU execution
// No HTTP/Router dependencies — just computation

// Ultra-simple Turing machine: 3 states, run N steps
// No imports, no stdlib, just arrays and arithmetic

export function runTuringMachine(machineId: number): number {
    // Batch 32 machines (like 1 warp)
    // All execute identical code path

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

    return ones;  // Return count of ones as result
}

// Batch kernel: run 32 machines in parallel
export function runBatch(): number {
    let totalOnes = 0;

    for (let m = 0; m < 32; m++) {
        const ones = runTuringMachine(m);
        totalOnes = totalOnes + ones;
    }

    return totalOnes;
}
