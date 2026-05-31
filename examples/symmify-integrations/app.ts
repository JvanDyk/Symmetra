/**
 * Complete integration example: MongoDB + Kafka + RabbitMQ with @symmetra/symmify
 *
 * This example demonstrates using all three integration packages together
 * to build a real-world microservice with:
 * - MongoDB for persistent data storage
 * - Kafka for audit trail and event streaming
 * - RabbitMQ for task queuing
 */

import symmify from '@symmetra/symmify'
import mongo from '@symmetra/mongo'
import kafka from '@symmetra/kafka'
import rabbitmq from '@symmetra/rabbitmq'
import { z } from 'zod'
import { ObjectId } from 'mongodb'

interface OrderEvent {
  orderId: string
  userId: string
  amount: number
  items: Array<{ sku: string; qty: number }>
  timestamp: string
}

interface EmailTask {
  to: string
  subject: string
  template: string
  data: Record<string, unknown>
  retries: number
}

// Initialize Symmify with all three integrations
const app = symmify({ logger: true })

// Register MongoDB
await app.register(mongo, {
  url: process.env.MONGO_URL || 'mongodb://localhost:27017',
  database: 'ecommerce',
})

// Register Kafka for event streaming
await app.register(kafka, {
  kafka: {
    clientId: 'order-api',
    brokers: (process.env.KAFKA_BROKERS || 'localhost:9092').split(','),
  },
  producer: true,
  consumer: { groupId: 'order-api-consumer' },
  admin: true,
})

// Register RabbitMQ for task queuing
await app.register(rabbitmq, {
  url: process.env.RABBITMQ_URL || 'amqp://guest:guest@localhost:5672/',
  prefetch: 10,
})

// ============================================================================
// Lifecycle: Setup on ready
// ============================================================================

app.addHook('onReady', async () => {
  console.log('✅ Setting up RabbitMQ exchanges and queues...')
  const rabbit = app.rabbitmq

  // Create email queue for background processing
  await rabbit.assertExchange('mailer', 'direct', { durable: true })
  await rabbit.assertQueue('emails', { durable: true })
  await rabbit.channel.bindQueue('emails', 'mailer', 'send')

  // Create payments exchange for payment processing
  await rabbit.assertExchange('payments', 'topic', { durable: true })
  await rabbit.assertQueue('payment-processing', { durable: true })
  await rabbit.channel.bindQueue('payment-processing', 'payments', 'process')

  console.log('✅ Setting up Kafka topics...')
  // Create Kafka topics for auditing
  const admin = app.kafka.admin
  try {
    await admin?.createTopics({
      topics: [
        { topic: 'orders', numPartitions: 3, replicationFactor: 1 },
        { topic: 'payments', numPartitions: 3, replicationFactor: 1 },
        { topic: 'audit', numPartitions: 1, replicationFactor: 1 },
      ],
      validateOnly: false,
    })
  } catch (err) {
    // Topics may already exist
  }

  console.log('✅ All integrations ready!')
})

// ============================================================================
// Routes: Order Creation (demonstrates all three systems)
// ============================================================================

app.post(
  '/api/orders',
  {
    schema: {
      body: z.object({
        userId: z.string().uuid(),
        items: z.array(
          z.object({
            sku: z.string(),
            qty: z.number().int().positive(),
          })
        ),
        shippingAddress: z.object({
          street: z.string(),
          city: z.string(),
          country: z.string(),
        }),
      }),
    },
  },
  async (req, reply) => {
    const orderId = new ObjectId().toHexString()
    const totalAmount = req.body.items.reduce((sum, item) => sum + item.qty, 0) * 100 // $100 per item

    const orderData = {
      _id: new ObjectId(orderId),
      orderId,
      userId: req.body.userId,
      items: req.body.items,
      shippingAddress: req.body.shippingAddress,
      amount: totalAmount,
      status: 'pending',
      createdAt: new Date(),
      createdBy: req.id, // Request ID for tracing
    }

    // 1️⃣ STORE in MongoDB
    try {
      await app.mongo.collection('orders').insertOne(orderData)
      req.log.info(`Created order ${orderId} in MongoDB`)
    } catch (err) {
      req.log.error(err, 'Failed to create order in MongoDB')
      return reply
        .code(500)
        .send({ error: 'Failed to create order', orderId })
    }

    // 2️⃣ PUBLISH to Kafka for audit trail
    try {
      await app.kafka.producer?.send({
        topic: 'orders',
        messages: [
          {
            key: req.body.userId,
            value: JSON.stringify({
              type: 'order.created',
              orderId,
              userId: req.body.userId,
              amount: totalAmount,
              timestamp: new Date().toISOString(),
              requestId: req.id,
            }),
            headers: {
              'correlation-id': req.id,
              'event-version': '1',
            },
          },
        ],
      })
      req.log.info(`Published order event to Kafka`)
    } catch (err) {
      req.log.error(err, 'Failed to publish to Kafka')
      // Non-critical: continue even if audit fails
    }

    // 3️⃣ QUEUE email notification in RabbitMQ
    try {
      app.rabbitmq.publish('mailer', 'send', {
        to: req.body.userId, // In real app: look up email from DB
        subject: `Order ${orderId} Confirmed`,
        template: 'order-confirmation',
        data: {
          orderId,
          items: req.body.items,
          totalAmount: `$${totalAmount / 100}`,
        },
        retries: 0,
      })
      req.log.info(`Queued confirmation email`)
    } catch (err) {
      req.log.error(err, 'Failed to queue email')
      // Non-critical: continue even if email queue fails
    }

    // Return success to client (202 Accepted - processing async)
    reply.code(202).send({
      orderId,
      status: 'pending',
      message: 'Order created. Confirmation email will be sent shortly.',
    })
  }
)

// ============================================================================
// Routes: Query Orders from MongoDB
// ============================================================================

app.get('/api/orders', async (req, reply) => {
  const orders = await app.mongo
    .collection('orders')
    .find({})
    .sort({ createdAt: -1 })
    .limit(50)
    .toArray()

  reply.send({
    count: orders.length,
    orders: orders.map((o: any) => ({
      orderId: o.orderId,
      userId: o.userId,
      amount: `$${o.amount / 100}`,
      status: o.status,
      createdAt: o.createdAt,
    })),
  })
})

app.get('/api/orders/:id', async (req, reply) => {
  const order = await app.mongo
    .collection('orders')
    .findOne({ orderId: req.params.id })

  if (!order) {
    return reply.code(404).send({ error: 'Order not found' })
  }

  reply.send({
    ...order,
    amount: `$${order.amount / 100}`,
  })
})

// ============================================================================
// Routes: Process Payment (demonstrates Kafka + RabbitMQ interaction)
// ============================================================================

app.post(
  '/api/orders/:id/pay',
  {
    schema: {
      body: z.object({
        paymentMethod: z.enum(['card', 'bank', 'crypto']),
      }),
    },
  },
  async (req, reply) => {
    const orderId = req.params.id

    // Update order status
    const updateResult = await app.mongo
      .collection('orders')
      .updateOne({ orderId }, { $set: { status: 'paying' } })

    if (updateResult.matchedCount === 0) {
      return reply.code(404).send({ error: 'Order not found' })
    }

    // Publish payment event to Kafka
    await app.kafka.producer?.send({
      topic: 'payments',
      messages: [
        {
          key: orderId,
          value: JSON.stringify({
            type: 'payment.initiated',
            orderId,
            method: req.body.paymentMethod,
            timestamp: new Date().toISOString(),
          }),
        },
      ],
    })

    // Queue payment processing task
    app.rabbitmq.publish('payments', 'process', {
      orderId,
      method: req.body.paymentMethod,
      retries: 0,
    })

    reply.code(202).send({ orderId, status: 'paying', message: 'Payment processing...' })
  }
)

// ============================================================================
// Routes: Monitoring
// ============================================================================

app.get('/health', async (req, reply) => {
  const mongoConnected = !!(await app.mongo.admin().ping())
  const kafkaConnected = !!app.kafka.producer
  const rabbitConnected = !!app.rabbitmq.connection

  reply.send({
    status: mongoConnected && kafkaConnected && rabbitConnected ? 'healthy' : 'degraded',
    integrations: {
      mongodb: mongoConnected ? '✅' : '❌',
      kafka: kafkaConnected ? '✅' : '❌',
      rabbitmq: rabbitConnected ? '✅' : '❌',
    },
  })
})

// ============================================================================
// Lifecycle: Graceful shutdown
// ============================================================================

app.addHook('onClose', async () => {
  console.log('Shutting down...')
  // All integrations handle their own cleanup via their onClose hooks
  console.log('✅ Shutdown complete')
})

// ============================================================================
// Start Server
// ============================================================================

const port = parseInt(process.env.PORT || '3000')
const url = await app.listen({ port, host: '0.0.0.0' })
console.log(`🚀 Server running at ${url}`)
console.log('Available endpoints:')
console.log(`  POST   ${url}/api/orders          (create order)')
console.log(`  GET    ${url}/api/orders          (list orders)`)
console.log(`  GET    ${url}/api/orders/:id      (get order)`)
console.log(`  POST   ${url}/api/orders/:id/pay  (process payment)`)
console.log(`  GET    ${url}/health              (health check)`)
