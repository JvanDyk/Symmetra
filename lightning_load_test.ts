// Lightning Load Test - Max Throughput Benchmark
// Runs pure compute kernel (Rule 60 XOR 32x unroll) at max scale
// Target: Measure peak RPS with async callback batching

export function runLightningBurst(machineId: number): number {
    // Pure XOR computation - 32 iterations, branchless
    let acc = machineId;

    // Rule 60: a XOR (b OR c)
    for (let i = 0; i < 32; i++) {
        const b = acc >> 1;
        const c = acc << 1;
        acc = acc ^ (b | c);
    }

    return acc % 256;
}

// Batch of 32 parallel computations (one per warp thread)
export function runLightningBatch(): number {
    let totalResult = 0;
    for (let m = 0; m < 32; m++) {
        totalResult = totalResult + runLightningBurst(m);
    }
    return totalResult;
}

// HTTP handler - called for each request
export default async function handler(req: Request): Promise<Response> {
    try {
        const result = runLightningBatch();
        return new Response(
            JSON.stringify({
                success: true,
                result: result,
                timestamp: Date.now()
            }),
            {
                status: 200,
                headers: { "Content-Type": "application/json" }
            }
        );
    } catch (e: unknown) {
        const msg = e instanceof Error ? e.message : String(e);
        return new Response(
            JSON.stringify({ error: msg }),
            {
                status: 500,
                headers: { "Content-Type": "application/json" }
            }
        );
    }
}
