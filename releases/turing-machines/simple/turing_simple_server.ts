import { serve, Router } from '@symmetra/http';

function runSimple(): number {
    const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    let state = 0, pos = 20, steps = 0;
    while (steps < 500 && state !== -1) {
        if (pos < 0 || pos >= 40) { state = -1; break; }
        if (state === 0) {
            if (tape[pos] === 0) { tape[pos] = 1; pos++; state = 1; }
            else { tape[pos] = 0; pos--; state = 2; }
        } else if (state === 1) {
            if (tape[pos] === 0) { tape[pos] = 1; pos++; state = 0; }
            else { tape[pos] = 1; pos--; state = 0; }
        } else if (state === 2) {
            if (tape[pos] === 0) { tape[pos] = 0; pos++; state = 1; }
            else { tape[pos] = 0; pos--; state = 1; }
        }
        steps++;
    }
    let ones = 0;
    for (let i = 0; i < 40; i++) if (tape[i] === 1) ones++;
    return ones;
}

const app = new Router();
app.get('/', (ctx) => { ctx.json({ result: runSimple(), variant: 'SIMPLE' }); });
serve(app, { port: 8007 });
