/**
 * Kafka Producer Example with @symmetra/symmify
 *
 * HTTP API that publishes events to Kafka topics.
 * Run this with: npm run dev
 */

import symmify from '@symmetra/symmify'
import kafka from '@symmetra/kafka'
import { z } from 'zod'

const app = symmify({ logger: true })

// Register Kafka plugin
await app.register(kafka, {
  kafka: {
    clientId: 'producer-api',
    brokers: (process.env.KAFKA_BROKERS || 'localhost:9092').split(','),
  },
  producer: true,
  admin: true,
})

// Setup topics on startup
app.addHook('onReady', async () => {
  console.log('Setting up Kafka topics...')
  try {
    await app.kafka.admin?.createTopics({
      topics: [
        { topic: 'user-events', numPartitions: 3, replicationFactor: 1 },
        { topic: 'order-events', numPartitions: 3, replicationFactor: 1 },
      ],
      validateOnly: false,
    })
    console.log('✅ Topics ready')
  } catch (err: any) {
    if (err.message?.includes('already exists')) {
      console.log('✅ Topics already exist')
    } else {
      console.error('Topic creation error:', err.message)
    }
  }
})

// ============================================================================
// Publish user event
// ============================================================================
app.post(
  '/events/user',
  {
    schema: {
      body: z.object({
        userId: z.string().uuid(),
        action: z.enum(['created', 'updated', 'deleted']),
        data: z.record(z.unknown()),
      }),
    },
  },
  async (req, reply) => {
    const { userId, action, data } = req.body

    try {
      const result = await app.kafka.producer!.send({
        topic: 'user-events',
        messages: [
          {
            key: userId,
            value: JSON.stringify({
              type: `user.${action}`,
              userId,
              data,
              timestamp: new Date().toISOString(),
              requestId: req.id,
            }),
            headers: {
              'correlation-id': req.id,
              'event-version': '1.0',
              'source': 'api',
            },
          },
        ],
      })

      req.log.info(`Published user.${action} event for user ${userId}`)
      reply.code(202).send({
        message: 'Event published',
        partition: result[0].partition,
        offset: result[0].offset,
      })
    } catch (err) {
      req.log.error(err, 'Failed to publish user event')
      reply.code(500).send({ error: 'Failed to publish event' })
    }
  }
)

// ============================================================================
// Publish order event
// ============================================================================
app.post(
  '/events/order',
  {
    schema: {
      body: z.object({
        orderId: z.string().uuid(),
        userId: z.string().uuid(),
        amount: z.number().positive(),
        status: z.enum(['created', 'processing', 'completed', 'cancelled']),
      }),
    },
  },
  async (req, reply) => {
    const { orderId, userId, amount, status } = req.body

    try {
      const result = await app.kafka.producer!.send({
        topic: 'order-events',
        messages: [
          {
            key: orderId,
            value: JSON.stringify({
              type: `order.${status}`,
              orderId,
              userId,
              amount,
              timestamp: new Date().toISOString(),
              requestId: req.id,
            }),
            headers: {
              'correlation-id': req.id,
              'user-id': userId,
            },
          },
        ],
      })

      req.log.info(`Published order.${status} event for order ${orderId}`)
      reply.code(202).send({
        message: 'Order event published',
        partition: result[0].partition,
        offset: result[0].offset,
      })
    } catch (err) {
      req.log.error(err, 'Failed to publish order event')
      reply.code(500).send({ error: 'Failed to publish event' })
    }
  }
)

// ============================================================================
// Batch publish events
// ============================================================================
app.post(
  '/events/batch',
  {
    schema: {
      body: z.object({
        events: z.array(
          z.object({
            topic: z.enum(['user-events', 'order-events']),
            key: z.string(),
            data: z.record(z.unknown()),
          })
        ),
      }),
    },
  },
  async (req, reply) => {
    const { events } = req.body

    try {
      const messages = events.map((event) => ({
        topic: event.topic,
        messages: [
          {
            key: event.key,
            value: JSON.stringify({
              ...event.data,
              timestamp: new Date().toISOString(),
              requestId: req.id,
            }),
            headers: {
              'correlation-id': req.id,
            },
          },
        ],
      }))

      const results = await app.kafka.producer!.sendBatch({
        topicMessages: messages,
      })

      req.log.info(`Published ${events.length} events`)
      reply.code(202).send({
        message: `Published ${events.length} events`,
        results: results.map((r) => ({
          partition: r.partition,
          offset: r.offset,
        })),
      })
    } catch (err) {
      req.log.error(err, 'Failed to publish batch events')
      reply.code(500).send({ error: 'Failed to publish events' })
    }
  }
)

// ============================================================================
// Health check
// ============================================================================
app.get('/health', async (req, reply) => {
  const kafka = app.kafka.producer ? '✅' : '❌'
  reply.send({ status: 'ok', kafka })
})

// ============================================================================
// Start server
// ============================================================================
const port = parseInt(process.env.PORT || '3001')
const url = await app.listen({ port, host: '127.0.0.1' })

console.log(`
✅ Kafka Producer running at ${url}

📤 Publish Endpoints:
  POST ${url}/events/user      (publish user event)
  POST ${url}/events/order     (publish order event)
  POST ${url}/events/batch     (publish batch events)
  GET  ${url}/health           (health check)

🧪 Test it:
  curl -X POST ${url}/events/user \\
    -H "Content-Type: application/json" \\
    -d '{
      "userId":"550e8400-e29b-41d4-a716-446655440000",
      "action":"created",
      "data":{"name":"Alice","email":"alice@example.com"}
    }'
`)

process.on('SIGINT', async () => {
  console.log('\n⏹️  Shutting down...')
  await app.close()
  process.exit(0)
})
