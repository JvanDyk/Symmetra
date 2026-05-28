// Moderate Turing Machine: Targeted 50K RPS
// 4-state machine on 64-cell tape — between SIMPLE and REFERENCE
// Practical complexity for realistic workloads

export function runModerateTuringMachine(machineId: number): number {
    const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0, 0];

    let state = 0;
    let pos = 32;
    let steps = 0;
    const maxSteps = 600;

    // 4-state machine with moderate complexity
    while (steps < maxSteps && state !== -1) {
        if (pos < 0 || pos >= 64) {
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
                state = 3;
            }
        } else if (state === 2) {
            if (symbol === 0) {
                tape[pos] = 0;
                pos = pos + 1;
                state = 3;
            } else {
                tape[pos] = 0;
                pos = pos - 1;
                state = 1;
            }
        } else if (state === 3) {
            if (symbol === 0) {
                tape[pos] = 1;
                pos = pos + 1;
                state = 2;
            } else {
                tape[pos] = 0;
                pos = pos - 1;
                state = -1;
            }
        }

        steps = steps + 1;
    }

    let ones = 0;
    for (let i = 0; i < 64; i++) {
        if (tape[i] === 1) ones = ones + 1;
    }

    return ones;
}

export function runModerateBatch(): number {
    let totalOnes = 0;

    for (let m = 0; m < 32; m++) {
        const ones = runModerateTuringMachine(m);
        totalOnes = totalOnes + ones;
    }

    return totalOnes;
}
