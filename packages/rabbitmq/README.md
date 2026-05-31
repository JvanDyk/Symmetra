# @symmetra/rabbitmq

RabbitMQ plugin for `@symmetra/symmify`.

Provides amqplib integration with the Fastify-compatible Symmify HTTP framework via the plugin/decorator system. Enables both message publishing and consumption from HTTP routes.

## Installation

```bash
npm install @symmetra/rabbitmq @symmetra/symmify amqplib
```

## Usage

Register the plugin with your Symmify instance:

```typescript
import symmify from '@symmetra/symmify'
import rabbitmq from '@symmetra/rabbitmq'

const app = symmify()

await app.register(rabbitmq, {
  url: 'amqp://guest:guest@localhost:5672/',
  heartbeat: 60,
  prefetch: 10,
})

// HTTP route that publishes to RabbitMQ
app.post('/notify', async (req, reply) => {
  req.server.rabbitmq.sendToQueue('notifications', {
    userId: req.body.userId,
    message: req.body.message,
  })
  reply.send({ queued: true })
})

await app.listen({ port: 3000 })
```

## API

### Plugin Options

```typescript
interface RabbitMQPluginOptions {
  url: string | Options.Connect       // AMQP URL or connection options
  heartbeat?: number                  // Server heartbeat interval (default: 60)
  prefetch?: number                   // Channel prefetch count (default: 0)
}
```

### Decorators

Once registered, access RabbitMQ via:

```typescript
interface RabbitMQDecorator {
  connection: Connection
  channel: Channel
  publish(exchange: string, routingKey: string, content: Buffer | object): boolean
  sendToQueue(queue: string, content: Buffer | object): boolean
  subscribe(queue: string, handler: MessageHandler): Promise<string>
  assertQueue(queue: string, opts?: Options.AssertQueue): Promise<void>
  assertExchange(exchange: string, type: string, opts?: Options.AssertExchange): Promise<void>
}

type MessageHandler = (
  msg: ConsumeMessage,
  ack: () => void,
  nack: (requeue?: boolean) => void,
) => void | Promise<void>
```

## Examples

### Publish to Queue

```typescript
app.post('/send-email', async (req, reply) => {
  req.server.rabbitmq.sendToQueue('emails', {
    to: req.body.email,
    subject: 'Welcome!',
    template: 'welcome',
  })
  reply.send({ queued: true })
})
```

### Publish to Exchange

```typescript
app.post('/events', async (req, reply) => {
  req.server.rabbitmq.publish('events', 'order.created', {
    orderId: req.body.orderId,
    userId: req.body.userId,
    amount: req.body.amount,
  })
  reply.send({ published: true })
})
```

### Set Up Queues and Bindings

```typescript
app.addHook('onReady', async () => {
  const rabbit = app.rabbitmq

  // Declare exchanges
  await rabbit.assertExchange('events', 'topic', { durable: true })
  await rabbit.assertExchange('dlx', 'direct', { durable: true })

  // Declare queues
  await rabbit.assertQueue('orders', {
    durable: true,
    deadLetterExchange: 'dlx',
  })
  await rabbit.assertQueue('emails', { durable: true })

  // Bind queue to exchange
  await rabbit.channel.bindQueue('orders', 'events', 'order.*')
})
```

### Handle Messages (From HTTP Route)

```typescript
app.post('/process-queue/:queueName', async (req, reply) => {
  const { queueName } = req.params

  const consumerTag = await req.server.rabbitmq.subscribe(
    queueName,
    async (msg, ack, nack) => {
      try {
        const data = JSON.parse(msg.content.toString())
        console.log(`Processing message from ${queueName}:`, data)
        // Process the message...
        ack()
      } catch (err) {
        console.error(`Error processing message:`, err)
        nack(true) // Requeue on error
      }
    }
  )

  reply.send({ consuming: true, consumerTag })
})
```

## Full Example: Email Service

```typescript
import symmify from '@symmetra/symmify'
import rabbitmq from '@symmetra/rabbitmq'
import { z } from 'zod'

const app = symmify()

await app.register(rabbitmq, {
  url: 'amqp://guest:guest@localhost:5672/',
  prefetch: 5,
})

// Set up RabbitMQ on startup
app.addHook('onReady', async () => {
  const rabbit = app.rabbitmq
  await rabbit.assertExchange('mailer', 'direct', { durable: true })
  await rabbit.assertQueue('emails', { durable: true })
  await rabbit.channel.bindQueue('emails', 'mailer', 'send')
})

// HTTP endpoint: queue an email
app.post(
  '/send-email',
  {
    schema: {
      body: z.object({
        to: z.string().email(),
        subject: z.string(),
        template: z.string(),
      }),
    },
  },
  async (req, reply) => {
    const published = app.rabbitmq.publish('mailer', 'send', {
      ...req.body,
      requestId: req.id,
      timestamp: new Date(),
    })

    reply.code(published ? 202 : 500).send({
      queued: published,
      requestId: req.id,
    })
  }
)

// HTTP endpoint: consume from queue (or use separate worker)
app.post('/worker/start', async (req, reply) => {
  await app.rabbitmq.subscribe('emails', async (msg, ack, nack) => {
    try {
      const email = JSON.parse(msg.content.toString())
      console.log(`Sending email to ${email.to}...`)
      // Call your email service here
      ack()
    } catch (err) {
      console.error('Email error:', err)
      nack(true)
    }
  })

  reply.send({ worker: 'started' })
})

await app.listen({ port: 3000 })
```

## Lifecycle

- **On register**: Connects to RabbitMQ and creates a channel with configured prefetch
- **On close**: Closes channel and connection gracefully

## Type Safety

Full amqplib types available:

```typescript
req.server.rabbitmq.channel          // type: Channel (from amqplib)
req.server.rabbitmq.connection       // type: Connection (from amqplib)
```

## License

MIT
