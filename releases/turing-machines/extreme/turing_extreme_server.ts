import { serve, Router } from '@symmetra/http';

function runExtreme(): number {
    const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    let state = 0, pos = 6, steps = 0;
    while (steps < 400 && state !== -1) {
        if (pos < 0 || pos >= 12) { state = -1; break; }
        if (state === 0) {
            if (tape[pos] === 0) { tape[pos] = 1; pos++; }
            else { tape[pos] = 0; pos--; state = -1; }
        }
        steps++;
    }
    let ones = 0;
    for (let i = 0; i < 12; i++) if (tape[i] === 1) ones++;
    return ones;
}

const app = new Router();
app.get('/', (ctx) => { ctx.json({ result: runExtreme(), variant: 'EXTREME' }); });
serve(app, { port: 8003 });
