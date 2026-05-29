import { serve, Router } from '@symmetra/http';

function runSuperfast(): number {
    const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    let state = 0, pos = 10, steps = 0;
    while (steps < 600 && state !== -1) {
        if (pos < 0 || pos >= 20) { state = -1; break; }
        if (state === 0) {
            if (tape[pos] === 0) { tape[pos] = 1; pos++; }
            else { tape[pos] = 0; pos--; state = -1; }
        }
        steps++;
    }
    let ones = 0;
    for (let i = 0; i < 20; i++) if (tape[i] === 1) ones++;
    return ones;
}

const app = new Router();
app.get('/', (ctx) => { ctx.json({ result: runSuperfast(), variant: 'SUPERFAST' }); });
serve(app, { port: 8006 });
