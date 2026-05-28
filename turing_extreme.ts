// Extreme Turing Machine: Targeted 8M RPS
// Very small tape with minimal branching logic
// Single array flip operation, optimized for GPU pipeline

export function runExtremeTuringMachine(machineId: number): number {
    // Tiny 12-cell tape: minimal memory footprint
    const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

    let state = 0;
    let pos = 6;
    let steps = 0;
    const maxSteps = 400;

    // 1-state, 12-cell machine
    while (steps < maxSteps && state !== -1) {
        if (pos < 0 || pos >= 12) {
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
    for (let i = 0; i < 12; i++) {
        if (tape[i] === 1) ones = ones + 1;
    }

    return ones;
}

export function runExtremeBatch(): number {
    let totalOnes = 0;

    for (let m = 0; m < 32; m++) {
        const ones = runExtremeTuringMachine(m);
        totalOnes = totalOnes + ones;
    }

    return totalOnes;
}
