import { serve, Router } from '@symmetra/http';

const app = new Router();

app.get('/ping', (ctx) => {
  ctx.json({ message: 'pong', gpu: true });
});

serve(app, { port: 8080 });
