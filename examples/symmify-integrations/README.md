# Symmify Integrations Example

Complete real-world example showing **MongoDB**, **Kafka**, and **RabbitMQ** working together with **@symmetra/symmify**.

## What This Example Demonstrates

An **order processing microservice** that:

1. **Stores orders** in MongoDB (persistent data)
2. **Streams events** to Kafka (audit trail, analytics)
3. **Queues tasks** in RabbitMQ (async email, payment processing)

All three integrations work **seamlessly** via the Symmify plugin system.

## Architecture

```
Client Request
    ↓
[POST /api/orders] (Symmify HTTP Handler)
    ├─→ MongoDB: Store order document
    ├─→ Kafka: Publish order.created event
    └─→ RabbitMQ: Queue confirmation email
    ↓
202 Accepted (async processing)

Separately:
  Kafka Consumer → Data warehouse / Analytics
  RabbitMQ Consumer → Email service
```

## Quick Start

### Prerequisites

- Node.js 18+
- MongoDB (or `mongod` running locally)
- Kafka (or docker: `docker run -d confluent/kafka`)
- RabbitMQ (or docker: `docker run -d rabbitmq`)

### Run

```bash
npm install
npm run dev
```

Server starts at `http://localhost:3000`

### Test Endpoints

```bash
# Create an order
curl -X POST http://localhost:3000/api/orders \
  -H "Content-Type: application/json" \
  -d '{
    "userId": "123e4567-e89b-12d3-a456-426614174000",
    "items": [{"sku": "WIDGET-1", "qty": 2}],
    "shippingAddress": {"street": "123 Main St", "city": "Boston", "country": "US"}
  }'

# List orders
curl http://localhost:3000/api/orders

# Get order by ID
curl http://localhost:3000/api/orders/{orderId}

# Process payment
curl -X POST http://localhost:3000/api/orders/{orderId}/pay \
  -H "Content-Type: application/json" \
  -d '{"paymentMethod": "card"}'

# Health check
curl http://localhost:3000/health
```

## Code Highlights

### 1. Register All Three Integrations

```typescript
const app = symmify({ logger: true })

await app.register(mongo, {
  url: 'mongodb://localhost:27017',
  database: 'ecommerce',
})

await app.register(kafka, {
  kafka: { clientId: 'api', brokers: ['localhost:9092'] },
  producer: true,
})

await app.register(rabbitmq, {
  url: 'amqp://guest:guest@localhost:5672/',
})
```

### 2. Use All Three in a Single Handler

```typescript
app.post('/api/orders', async (req, reply) => {
  // 1. Store in MongoDB
  await app.mongo.collection('orders').insertOne(orderData)

  // 2. Publish to Kafka
  await app.kafka.producer.send({
    topic: 'orders',
    messages: [{ key: userId, value: JSON.stringify(event) }],
  })

  // 3. Queue task in RabbitMQ
  app.rabbitmq.publish('mailer', 'send', { to, subject, template })

  reply.code(202).send({ orderId, status: 'pending' })
})
```

### 3. Type-Safe Access

```typescript
// TypeScript knows about these!
req.server.mongo       // Db (MongoDB)
req.server.kafka       // KafkaDecorator (KafkaJS)
req.server.rabbitmq    // RabbitMQDecorator (amqplib)
```

## Real-World Patterns Shown

### Transactional Order Creation
- Write to primary store (MongoDB)
- Publish audit event (Kafka)
- Queue async tasks (RabbitMQ)
- Return 202 Accepted to client immediately

### Error Handling
- Critical failures (MongoDB) → 500 error
- Non-critical failures (email queue) → still returns 202 (best-effort)
- Proper logging at each step

### Lifecycle Management
- Setup exchanges/queues on `onReady`
- Graceful shutdown on `onClose`
- All cleanup handled automatically by plugins

## Environment Variables

```bash
MONGO_URL=mongodb://localhost:27017
KAFKA_BROKERS=localhost:9092
RABBITMQ_URL=amqp://guest:guest@localhost:5672/
PORT=3000
```

## Next Steps

To extend this example:

1. **Add consumers** — Kafka consumer in separate process for analytics
2. **Add rate limiting** — Symmify hooks for request throttling
3. **Add authentication** — JWT via `decorateRequest` + `preHandler` hook
4. **Add validation** — More detailed Zod schemas per route
5. **Add caching** — Redis plugin (same pattern as mongo/kafka/rabbitmq)

## Files in This Example

- `app.ts` — Main application with all routes and integrations
- `package.json` — Dependencies (includes all three plugins)
- `tsconfig.json` — TypeScript configuration
- `README.md` — This file

All three plugins work **out-of-the-box** via the Symmify plugin system! 🚀
