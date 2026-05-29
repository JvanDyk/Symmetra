import { serve, Router } from '@symmetra/http';

const app = new Router();

app.get('/', (ctx) => {
    ctx.json({ message: 'hello from GPU', status: 'ok' });
});

serve(app, { port: 8010 });
