import { serve, Router } from '@symmetra/http';

// Simple GPU workload — test async batch launching
const app = new Router();

app.get('/', (ctx) => {
    // Simulate some GPU work
    let result = 0;
    for (let i = 0; i < 100; i++) {
        result = result ^ (i << 1) | (i >> 1);
    }
    ctx.json({ message: 'gpu async test', result: result });
});

serve(app, { port: 8010 });
