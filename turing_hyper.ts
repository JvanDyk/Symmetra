// Hyper Turing Machine: Targeted 16M RPS
// Minimal state, minimal tape — balance between ULTRA and LIGHTNING
// 1-state machine on 15-cell tape with optimized counting

export function runHyperTuringMachine(machineId: number): number {
    const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

    let state = 0;
    let pos = 7;
    let steps = 0;
    const maxSteps = 300;

    // Ultra-fast 1-state machine
    while (steps < maxSteps && state !== -1) {
        if (pos < 0 || pos >= 15) {
            state = -1;
            break;
        }

        const symbol = tape[pos];

        if (state === 0) {
            if (symbol === 0) {
                tape[pos] = 1;
                pos = pos + 1;
            } else {
                tape[pos] = 0;
                pos = pos - 1;
                state = -1;
            }
        }

        steps = steps + 1;
    }

    let ones = 0;
    for (let i = 0; i < 15; i++) {
        if (tape[i] === 1) ones = ones + 1;
    }

    return ones;
}

export function runHyperBatch(): number {
    let totalOnes = 0;

    for (let m = 0; m < 32; m++) {
        const ones = runHyperTuringMachine(m);
        totalOnes = totalOnes + ones;
    }

    return totalOnes;
}
