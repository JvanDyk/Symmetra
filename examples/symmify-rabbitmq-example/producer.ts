/**
 * RabbitMQ Producer Example with @symmetra/symmify
 *
 * HTTP API that publishes and queues messages to RabbitMQ.
 * Run this with: npm run dev
 */

import symmify from '@symmetra/symmify'
import rabbitmq from '@symmetra/rabbitmq'
import { z } from 'zod'

const app = symmify({ logger: true })

// Register RabbitMQ plugin
await app.register(rabbitmq, {
  url: process.env.RABBITMQ_URL || 'amqp://guest:guest@localhost:5672/',
  prefetch: 10,
})

// Setup exchanges and queues on startup
app.addHook('onReady', async () => {
  console.log('Setting up RabbitMQ exchanges and queues...')
  const rabbit = app.rabbitmq

  try {
    // Create exchanges
    await rabbit.assertExchange('emails', 'direct', { durable: true })
    await rabbit.assertExchange('notifications', 'topic', { durable: true })
    await rabbit.assertExchange('tasks', 'fanout', { durable: true })

    // Create queues
    await rabbit.assertQueue('email-queue', { durable: true })
    await rabbit.assertQueue('notification-queue', { durable: true })
    await rabbit.assertQueue('task-queue', { durable: true })

    // Bind queues to exchanges
    await rabbit.channel.bindQueue('email-queue', 'emails', 'send')
    await rabbit.channel.bindQueue(
      'notification-queue',
      'notifications',
      'user.*'
    )
    await rabbit.channel.bindQueue('task-queue', 'tasks', '')

    console.log('✅ RabbitMQ setup complete')
  } catch (err: any) {
    console.error('RabbitMQ setup error:', err.message)
  }
})

// ============================================================================
// Send email (direct exchange)
// ============================================================================
app.post(
  '/queue/email',
  {
    schema: {
      body: z.object({
        to: z.string().email(),
        subject: z.string(),
        template: z.string(),
        data: z.record(z.unknown()).optional(),
      }),
    },
  },
  async (req, reply) => {
    const { to, subject, template, data } = req.body

    try {
      const success = app.rabbitmq.publish('emails', 'send', {
        to,
        subject,
        template,
        data,
        queuedAt: new Date().toISOString(),
        requestId: req.id,
      })

      if (!success) {
        return reply.code(500).send({ error: 'Failed to queue email' })
      }

      req.log.info(`Queued email to ${to}`)
      reply.code(202).send({
        message: 'Email queued',
        to,
        subject,
      })
    } catch (err) {
      req.log.error(err, 'Failed to queue email')
      reply.code(500).send({ error: 'Failed to queue email' })
    }
  }
)

// ============================================================================
// Send notification (topic exchange)
// ============================================================================
app.post(
  '/queue/notification',
  {
    schema: {
      body: z.object({
        userId: z.string().uuid(),
        type: z.enum(['login', 'logout', 'update', 'follow', 'message']),
        message: z.string(),
        data: z.record(z.unknown()).optional(),
      }),
    },
  },
  async (req, reply) => {
    const { userId, type, message, data } = req.body

    try {
      const routingKey = `user.${type}`
      const success = app.rabbitmq.publish('notifications', routingKey, {
        userId,
        type,
        message,
        data,
        sentAt: new Date().toISOString(),
      })

      if (!success) {
        return reply.code(500).send({ error: 'Failed to queue notification' })
      }

      req.log.info(`Queued notification for user ${userId}: ${type}`)
      reply.code(202).send({
        message: 'Notification queued',
        userId,
        type,
      })
    } catch (err) {
      req.log.error(err, 'Failed to queue notification')
      reply.code(500).send({ error: 'Failed to queue notification' })
    }
  }
)

// ============================================================================
// Queue task (fanout exchange)
// ============================================================================
app.post(
  '/queue/task',
  {
    schema: {
      body: z.object({
        taskId: z.string().uuid(),
        taskType: z.string(),
        priority: z.enum(['low', 'normal', 'high']).optional(),
        payload: z.record(z.unknown()),
      }),
    },
  },
  async (req, reply) => {
    const { taskId, taskType, priority, payload } = req.body

    try {
      const success = app.rabbitmq.sendToQueue('task-queue', {
        taskId,
        taskType,
        priority: priority || 'normal',
        payload,
        queuedAt: new Date().toISOString(),
      })

      if (!success) {
        return reply.code(500).send({ error: 'Failed to queue task' })
      }

      req.log.info(`Queued task ${taskId} (${taskType})`)
      reply.code(202).send({
        message: 'Task queued',
        taskId,
        taskType,
      })
    } catch (err) {
      req.log.error(err, 'Failed to queue task')
      reply.code(500).send({ error: 'Failed to queue task' })
    }
  }
)

// ============================================================================
// Direct queue send
// ============================================================================
app.post(
  '/queue/send/:queueName',
  {
    schema: {
      params: z.object({
        queueName: z.string(),
      }),
      body: z.record(z.unknown()),
    },
  },
  async (req, reply) => {
    const { queueName } = req.params
    const message = req.body

    try {
      const success = app.rabbitmq.sendToQueue(queueName, {
        ...message,
        sentAt: new Date().toISOString(),
      })

      if (!success) {
        return reply.code(500).send({ error: `Failed to queue message` })
      }

      req.log.info(`Queued message to ${queueName}`)
      reply.code(202).send({
        message: 'Message queued',
        queue: queueName,
      })
    } catch (err) {
      req.log.error(err, `Failed to queue to ${queueName}`)
      reply.code(500).send({ error: 'Failed to queue message' })
    }
  }
)

// ============================================================================
// Health check
// ============================================================================
app.get('/health', async (req, reply) => {
  const connected = app.rabbitmq.connection ? '✅' : '❌'
  reply.send({ status: 'ok', rabbitmq: connected })
})

// ============================================================================
// Start server
// ============================================================================
const port = parseInt(process.env.PORT || '3002')
const url = await app.listen({ port, host: '127.0.0.1' })

console.log(`
✅ RabbitMQ Producer running at ${url}

📨 Queue Endpoints:
  POST ${url}/queue/email           (send email)
  POST ${url}/queue/notification   (send notification)
  POST ${url}/queue/task           (queue task)
  POST ${url}/queue/send/:queue    (direct queue send)
  GET  ${url}/health               (health check)

🧪 Test it:
  curl -X POST ${url}/queue/email \\
    -H "Content-Type: application/json" \\
    -d '{
      "to":"user@example.com",
      "subject":"Welcome!",
      "template":"welcome",
      "data":{"name":"Alice"}
    }'

  curl -X POST ${url}/queue/notification \\
    -H "Content-Type: application/json" \\
    -d '{
      "userId":"550e8400-e29b-41d4-a716-446655440000",
      "type":"login",
      "message":"Welcome back Alice!",
      "data":{"ip":"192.168.1.1"}
    }'
`)

process.on('SIGINT', async () => {
  console.log('\n⏹️  Shutting down...')
  await app.close()
  process.exit(0)
})
