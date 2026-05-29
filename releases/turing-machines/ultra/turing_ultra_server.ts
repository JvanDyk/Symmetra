import { serve, Router } from '@symmetra/http';

function runUltra(): number {
    const tape = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    for (let i = 0; i < 10; i++) tape[i] = tape[i] === 0 ? 1 : 0;
    let ones = 0;
    for (let i = 0; i < 10; i++) if (tape[i] === 1) ones++;
    return ones;
}

const app = new Router();
app.get('/', (ctx) => { ctx.json({ result: runUltra(), variant: 'ULTRA' }); });
serve(app, { port: 8004 });
