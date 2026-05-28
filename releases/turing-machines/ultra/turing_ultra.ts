// ULTRA variant: Absolute minimum - no state, no branching
// Single pass through tiny tape with zero conditional logic
// Designed to maximize GPU instruction throughput

export function runUltraMachine(machineId: number): number {
    // Absolute minimum: 10 cells
    const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

    // Single position (no moving)
    const pos = 5;

    // Process: just flip values, no state machine
    for (let i = 0; i < 10; i++) {
        tape[i] = tape[i] === 0 ? 1 : 0;
    }

    // Count ones
    let ones = 0;
    for (let i = 0; i < 10; i++) {
        if (tape[i] === 1) ones = ones + 1;
    }

    return ones;
}

export function runUltraBatch(): number {
    let totalOnes = 0;

    // All 32 threads run identical ultra-fast machine
    for (let m = 0; m < 32; m++) {
        const ones = runUltraMachine(m);
        totalOnes = totalOnes + ones;
    }

    return totalOnes;
}
