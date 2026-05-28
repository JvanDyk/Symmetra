// LIGHTNING variant: Pure compute - no memory operations
// Arithmetic-only kernel that saturates GPU compute units
// Zero data dependencies, maximum instruction-level parallelism

export function runLightningMachine(machineId: number): number {
    // Pure arithmetic - no array access
    let acc = machineId;  // Use threadId for variation
    let result = 0;

    // 100 iterations of pure arithmetic (no branching)
    for (let i = 0; i < 100; i++) {
        acc = acc + 1;
        acc = acc * 2;
        acc = acc - 1;
        result = result + acc;
    }

    return result % 256;  // Return as 0-255
}

export function runLightningBatch(): number {
    let totalResult = 0;

    // All 32 threads run identical compute-only kernel
    for (let m = 0; m < 32; m++) {
        const result = runLightningMachine(m);
        totalResult = totalResult + result;
    }

    return totalResult;
}
