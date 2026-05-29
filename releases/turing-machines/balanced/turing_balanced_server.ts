import { serve, Router } from '@symmetra/http';

function runBalanced(): number {
    const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    let state = 0, pos = 12, steps = 0;
    while (steps < 450 && state !== -1) {
        if (pos < 0 || pos >= 25) { state = -1; break; }
        if (state === 0) {
            if (tape[pos] === 0) { tape[pos] = 1; pos++; state = 1; }
            else { tape[pos] = 0; pos--; state = -1; }
        } else if (state === 1) {
            if (tape[pos] === 0) { tape[pos] = 1; pos++; state = 0; }
            else { tape[pos] = 0; pos--; state = -1; }
        }
        steps++;
    }
    let ones = 0;
    for (let i = 0; i < 25; i++) if (tape[i] === 1) ones++;
    return ones;
}

const app = new Router();
app.get('/', (ctx) => { ctx.json({ result: runBalanced(), variant: 'BALANCED' }); });
serve(app, { port: 8005 });
