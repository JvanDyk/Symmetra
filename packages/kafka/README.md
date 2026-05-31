# @symmetra/kafka

Kafka plugin for `@symmetra/symmify`.

Provides KafkaJS integration with the Fastify-compatible Symmify HTTP framework via the plugin/decorator system. Cleanly separates the HTTP API producer from message consumers.

## Installation

```bash
npm install @symmetra/kafka @symmetra/symmify kafkajs
```

## Usage

Register the plugin with your Symmify instance:

```typescript
import symmify from '@symmetra/symmify'
import kafka from '@symmetra/kafka'

const app = symmify()

await app.register(kafka, {
  kafka: {
    clientId: 'myapp',
    brokers: ['localhost:9092'],
  },
  producer: true,
  consumer: { groupId: 'myapp-group' },
})

// HTTP route that produces to Kafka
app.post('/events', async (req, reply) => {
  await req.server.kafka.producer.send({
    topic: 'events',
    messages: [
      { key: req.body.userId, value: JSON.stringify(req.body) },
    ],
  })
  reply.send({ sent: true })
})

await app.listen({ port: 3000 })
```

## API

### Plugin Options

```typescript
interface KafkaPluginOptions {
  kafka: KafkaConfig                  // KafkaJS client config
  producer?: boolean | ProducerConfig // Enable/config producer (default: false)
  consumer?: ConsumerConfig           // Enable/config consumer with groupId
  admin?: boolean                     // Enable admin client (default: false)
}
```

### Decorators

Once registered, access Kafka clients via:

```typescript
interface KafkaDecorator {
  kafka: Kafka                        // The KafkaJS instance
  producer: Producer | null           // Producer client (if enabled)
  consumer: Consumer | null           // Consumer client (if enabled)
  admin: Admin | null                 // Admin client (if enabled)
}
```

## Example: Event Producer API

```typescript
import symmify from '@symmetra/symmify'
import kafka from '@symmetra/kafka'
import { z } from 'zod'

const app = symmify()

await app.register(kafka, {
  kafka: {
    clientId: 'orders-api',
    brokers: ['kafka1:9092', 'kafka2:9092'],
  },
  producer: {
    idempotent: true,
    maxInFlightRequests: 5,
    compression: 1, // Gzip
  },
})

// Publish order events
app.post(
  '/orders',
  {
    schema: {
      body: z.object({
        userId: z.string(),
        items: z.array(z.object({ sku: z.string(), qty: z.number() })),
      }),
    },
  },
  async (req, reply) => {
    const order = req.body

    // Publish to Kafka
    await req.server.kafka.producer.send({
      topic: 'orders',
      messages: [
        {
          key: order.userId,
          value: JSON.stringify({
            ...order,
            timestamp: new Date().toISOString(),
          }),
          headers: {
            'event-type': 'order.created',
            'correlation-id': req.id,
          },
        },
      ],
    })

    reply.code(201).send({ orderId: 'generated-id', published: true })
  }
)

await app.listen({ port: 3000 })
```

## Important Note: Consumers

This plugin handles Kafka **producers** (sending messages from HTTP routes). **Consumers** should run in a separate, standalone process:

```typescript
// consumer.ts (separate Node.js process)
import kafka from '@symmetra/kafka'

const kafkaInstance = new Kafka({
  clientId: 'orders-consumer',
  brokers: ['kafka1:9092'],
})

const consumer = kafkaInstance.consumer({ groupId: 'orders-group' })
await consumer.connect()
await consumer.subscribe({ topic: 'orders', fromBeginning: false })

await consumer.run({
  eachMessage: async ({ topic, partition, message }) => {
    const event = JSON.parse(message.value.toString())
    console.log(`Received order event:`, event)
    // Process event: update DB, send email, etc.
  },
})
```

## Lifecycle

- **On register**: Initializes Kafka producers, consumers, and admin clients as configured
- **On close**: Gracefully disconnects all Kafka clients

## Type Safety

Full KafkaJS types available:

```typescript
req.server.kafka.producer    // type: Producer | null
req.server.kafka.consumer    // type: Consumer | null
req.server.kafka.admin       // type: Admin | null
```

## License

MIT
