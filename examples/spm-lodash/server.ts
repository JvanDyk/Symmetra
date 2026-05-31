// lodash example: Pure computation on GPU via npm package
// NO CODE CHANGES - uses npm lodash exactly as in Node.js

import { serve, Router } from '@symmetra/http';
import _ from 'lodash';

const app = new Router();

// Test 1: Array operations
app.post('/unique', (ctx) => {
  const data = ctx.body<number[]>();
  const unique = _.uniq(data);
  ctx.json({ input: data, unique, count: unique.length });
});

// Test 2: Grouping
app.post('/group', (ctx) => {
  const items = ctx.body<Array<{ category: string; value: number }>>();
  const grouped = _.groupBy(items, 'category');
  ctx.json({ grouped });
});

// Test 3: Filtering and mapping
app.post('/filter-map', (ctx) => {
  const numbers = ctx.body<number[]>();
  const filtered = _.filter(numbers, n => n > 5);
  const mapped = _.map(filtered, n => n * 2);
  ctx.json({ input: numbers, filtered, mapped });
});

// Test 4: Complex data processing
app.post('/process', (ctx) => {
  const data = ctx.body<Array<any>>();
  const result = _(data)
    .groupBy('type')
    .mapValues(items => ({
      count: items.length,
      sum: _.sumBy(items, 'value'),
      avg: _.meanBy(items, 'value'),
      max: _.maxBy(items, 'value'),
      min: _.minBy(items, 'value')
    }))
    .value();

  ctx.json(result);
});

serve(app, { port: 3001 });
console.log('Lodash GPU server running on port 3001');
