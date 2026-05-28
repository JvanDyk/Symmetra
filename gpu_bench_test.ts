export default async function handler(req: Request): Promise<Response> {
    let r = 0;
    for (let i = 0; i < 32; i++) {
        let a = i;
        for (let j = 0; j < 256; j++) {
            a = a ^ ((a << 1) | (a >> 1));
        }
        r += a;
    }
    return new Response(JSON.stringify({ok: true, r: r}), {status: 200, headers: {"Content-Type": "application/json"}});
}
