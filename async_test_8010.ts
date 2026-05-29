import { serve, Router } from '@symmetra/http';

const app = new Router();

app.get('/', (ctx) => {
    let result = 0;
    for (let i = 0; i < 100; i++) {
        result = result ^ (i << 1) | (i >> 1);
    }
    ctx.json({ message: 'async test', result: result });
});

serve(app, { port: 8010 });
