// Balanced Turing Machine: Targeted 1M RPS
// 2-state machine on 25-cell tape — practical balance
// Between SUPERFAST and ULTRA in performance

export function runBalancedTuringMachine(machineId: number): number {
    const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0];

    let state = 0;
    let pos = 12;
    let steps = 0;
    const maxSteps = 450;

    // 2-state, 25-cell machine
    while (steps < maxSteps && state !== -1) {
        if (pos < 0 || pos >= 25) {
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
                state = -1;
            }
        } else if (state === 1) {
            if (symbol === 0) {
                tape[pos] = 1;
                pos = pos + 1;
                state = 0;
            } else {
                tape[pos] = 0;
                pos = pos - 1;
                state = -1;
            }
        }

        steps = steps + 1;
    }

    let ones = 0;
    for (let i = 0; i < 25; i++) {
        if (tape[i] === 1) ones = ones + 1;
    }

    return ones;
}

export function runBalancedBatch(): number {
    let totalOnes = 0;

    for (let m = 0; m < 32; m++) {
        const ones = runBalancedTuringMachine(m);
        totalOnes = totalOnes + ones;
    }

    return totalOnes;
}
