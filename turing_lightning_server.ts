// LIGHTNING variant with HTTP server
// Tests async batching with proper route registration

export function runLightningMachine(machineId: number): number {
    // Pure arithmetic - no array access
    let acc = machineId;
    let result = 0;

    // 100 iterations of pure arithmetic
    for (let i = 0; i < 100; i++) {
        acc = acc + 1;
        acc = acc * 2;
        acc = acc - 1;
        result = result + acc;
    }

    return result % 256;
}

export function runLightningBatch(): number {
    let totalResult = 0;
    for (let m = 0; m < 32; m++) {
        const result = runLightningMachine(m);
        totalResult = totalResult + result;
    }
    return totalResult;
}

// HTTP handler that calls the Lightning kernel
export default async function handler(req: Request): Promise<Response> {
    try {
        const result = runLightningBatch();
        return new Response(JSON.stringify({ success: true, result: result }), {
            status: 200,
            headers: { "Content-Type": "application/json" }
        });
    } catch (e: unknown) {
        const msg = e instanceof Error ? e.message : String(e);
        return new Response(JSON.stringify({ error: msg }), {
            status: 500,
            headers: { "Content-Type": "application/json" }
        });
    }
}

// Start server on port 9000
serve({ port: 9000, handler: default as any });
