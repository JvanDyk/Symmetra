// Super-Fast Turing Machine: 1-state, minimal tape
// Absolute minimum complexity for maximum throughput
// Single state machine with immediate halt condition

export function runSuperFastTuringMachine(machineId: number): number {
    // Ultra-minimal tape (20 cells)
    const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

    let state = 0;
    let pos = 10;  // Center
    let steps = 0;
    const maxSteps = 500;

    // 1-state machine: maximum speed (minimal branching)
    while (steps < maxSteps && state === 0) {
        if (pos < 0 || pos >= 20) {
            state = -1;  // Halt at boundary
            break;
        }

        const symbol = tape[pos];

        // Single state: just flip and move right
        if (symbol === 0) {
            tape[pos] = 1;
            pos = pos + 1;
        } else {
            pos = pos - 1;  // Move left if already 1
        }

        steps = steps + 1;
    }

    // Count ones on tape
    let ones = 0;
    for (let i = 0; i < 20; i++) {
        if (tape[i] === 1) {
            ones = ones + 1;
        }
    }

    return ones;
}

export function runSuperFastBatch(): number {
    let totalOnes = 0;

    // All 32 threads run identical super-fast machine
    for (let m = 0; m < 32; m++) {
        const ones = runSuperFastTuringMachine(m);
        totalOnes = totalOnes + ones;
    }

    return totalOnes;
}
