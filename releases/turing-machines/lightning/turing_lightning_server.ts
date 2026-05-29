import { serve, Router } from '@symmetra/http';

// LIGHTNING: Pure arithmetic kernel (100 iterations, zero memory ops)
function runLightning(): number {
    let result = 0;
    for (let i = 0; i < 100; i++) {
        result = result + i;
        result = result * 2;
        result = result - i;
    }
    return result;
}

const app = new Router();

app.get('/', (ctx) => {
    const result = runLightning();
    ctx.json({ result: result, variant: 'LIGHTNING' });
});

serve(app, { port: 8001 });
