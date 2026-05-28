// Fastest Turing Machine: Minimal state transitions, smallest tape
// 2-state, 30-cell tape — designed for maximum GPU throughput
// Simpler logic = faster per-step execution = higher total throughput

export function runFastTuringMachine(machineId: number): number {
    // Minimal tape (30 cells instead of 40)
    const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

    let state = 0;
    let pos = 15;  // Center (15 for 30-cell tape)
    let steps = 0;
    const maxSteps = 500;

    // Super-fast 2-state machine (fewer state checks)
    while (steps < maxSteps && state !== -1) {
        if (pos < 0 || pos >= 30) {
            state = -1;
            break;
        }

        const symbol = tape[pos];

        // 2-state machine: much faster than 3-state
        if (state === 0) {
            if (symbol === 0) {
                tape[pos] = 1;
                pos = pos + 1;
                state = 1;
            } else {
                tape[pos] = 0;
                pos = pos - 1;
                state = -1;  // Quick halt
            }
        } else if (state === 1) {
            if (symbol === 0) {
                tape[pos] = 1;
                pos = pos + 1;
                state = 0;
            } else {
                tape[pos] = 0;
                pos = pos - 1;
                state = -1;  // Quick halt
            }
        }

        steps = steps + 1;
    }

    // Count ones on tape
    let ones = 0;
    for (let i = 0; i < 30; i++) {
        if (tape[i] === 1) {
            ones = ones + 1;
        }
    }

    return ones;
}

export function runFastBatch(): number {
    let totalOnes = 0;

    // All 32 threads run identical fast machine
    for (let m = 0; m < 32; m++) {
        const ones = runFastTuringMachine(m);
        totalOnes = totalOnes + ones;
    }

    return totalOnes;
}
