import { serve, Router } from '@symmetra/http';

const app = new Router();
app.get('/', (ctx) => {
    ctx.json({ ok: true });
});

serve(app, { port: 8010 });
