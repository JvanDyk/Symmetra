import { serve, Router } from '@symmetra/http';

function runReference(): number {
    const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    let state = 0, pos = 64, steps = 0;
    while (steps < 1000 && state !== -1) {
        if (pos < 0 || pos >= 128) { state = -1; break; }
        if (state === 0) {
            if (tape[pos] === 0) { tape[pos] = 1; pos++; state = 1; }
            else { tape[pos] = 0; pos--; state = 2; }
        } else if (state === 1) {
            if (tape[pos] === 0) { tape[pos] = 1; pos++; state = 0; }
            else { tape[pos] = 1; pos--; state = 3; }
        } else if (state === 2) {
            if (tape[pos] === 0) { tape[pos] = 0; pos++; state = 3; }
            else { tape[pos] = 0; pos--; state = 4; }
        } else if (state === 3) {
            if (tape[pos] === 0) { tape[pos] = 1; pos++; state = 2; }
            else { tape[pos] = 0; pos--; state = 1; }
        } else if (state === 4) {
            if (tape[pos] === 0) { tape[pos] = 1; pos++; state = 3; }
            else { tape[pos] = 0; pos--; state = -1; }
        }
        steps++;
    }
    let ones = 0;
    for (let i = 0; i < 128; i++) if (tape[i] === 1) ones++;
    return ones;
}

const app = new Router();
app.get('/', (ctx) => { ctx.json({ result: runReference(), variant: 'REFERENCE' }); });
serve(app, { port: 8009 });
