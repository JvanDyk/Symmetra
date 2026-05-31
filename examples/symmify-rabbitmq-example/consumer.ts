/**
 * RabbitMQ Consumer Example
 *
 * Standalone consumer that processes messages from RabbitMQ queues.
 * Run this with: npx tsx consumer.ts
 */

import * as amqp from 'amqplib'

async function start() {
  try {
    // Connect to RabbitMQ
    const connection = await amqp.connect(
      process.env.RABBITMQ_URL || 'amqp://guest:guest@localhost:5672/'
    )
    console.log('✅ Connected to RabbitMQ')

    // Create channel
    const channel = await connection.createChannel()
    await channel.prefetch(1) // Process one message at a time
    console.log('✅ Channel created (prefetch: 1)')

    // Setup exchanges and queues
    await channel.assertExchange('emails', 'direct', { durable: true })
    await channel.assertExchange('notifications', 'topic', { durable: true })
    await channel.assertExchange('tasks', 'fanout', { durable: true })

    await channel.assertQueue('email-queue', { durable: true })
    await channel.assertQueue('notification-queue', { durable: true })
    await channel.assertQueue('task-queue', { durable: true })

    // Bind queues
    await channel.bindQueue('email-queue', 'emails', 'send')
    await channel.bindQueue('notification-queue', 'notifications', 'user.*')
    await channel.bindQueue('task-queue', 'tasks', '')

    console.log('✅ Queues and exchanges ready\n')

    // ====================================================================
    // Consume emails
    // ====================================================================
    console.log('📨 Email Consumer: Listening...')
    await channel.consume('email-queue', async (msg) => {
      if (!msg) return

      try {
        const content = JSON.parse(msg.content.toString())
        console.log(`
✉️  Email Message
   To: ${content.to}
   Subject: ${content.subject}
   Template: ${content.template}
   Data: ${JSON.stringify(content.data)}
   Queued At: ${content.queuedAt}
`)

        // Simulate email sending
        await new Promise((resolve) => setTimeout(resolve, 100))

        // Acknowledge message
        channel.ack(msg)
        console.log(`✅ Email sent to ${content.to}`)
      } catch (err) {
        console.error('❌ Error processing email:', err)
        channel.nack(msg, false, true) // Requeue on error
      }
    })

    // ====================================================================
    // Consume notifications
    // ====================================================================
    console.log('🔔 Notification Consumer: Listening...')
    await channel.consume('notification-queue', async (msg) => {
      if (!msg) return

      try {
        const content = JSON.parse(msg.content.toString())
        console.log(`
🔔 Notification Message
   User: ${content.userId}
   Type: ${content.type}
   Message: ${content.message}
   Data: ${JSON.stringify(content.data)}
`)

        // Simulate notification processing
        await new Promise((resolve) => setTimeout(resolve, 100))

        channel.ack(msg)
        console.log(
          `✅ Notification sent to user ${content.userId} (${content.type})`
        )
      } catch (err) {
        console.error('❌ Error processing notification:', err)
        channel.nack(msg, false, true)
      }
    })

    // ====================================================================
    // Consume tasks
    // ====================================================================
    console.log('⚙️  Task Consumer: Listening...')
    await channel.consume('task-queue', async (msg) => {
      if (!msg) return

      try {
        const content = JSON.parse(msg.content.toString())
        console.log(`
⚙️  Task Message
   Task ID: ${content.taskId}
   Type: ${content.taskType}
   Priority: ${content.priority}
   Payload: ${JSON.stringify(content.payload)}
   Queued At: ${content.queuedAt}
`)

        // Simulate task processing based on priority
        const delay = {
          high: 50,
          normal: 200,
          low: 500,
        }[content.priority] || 200

        await new Promise((resolve) => setTimeout(resolve, delay))

        channel.ack(msg)
        console.log(`✅ Task completed: ${content.taskId}`)
      } catch (err) {
        console.error('❌ Error processing task:', err)
        channel.nack(msg, false, true)
      }
    })

    console.log(`
💡 Consumers started!
   Waiting for messages on:
   - email-queue
   - notification-queue
   - task-queue

   Send messages via: npm run dev (producer)
`)

    // Graceful shutdown
    process.on('SIGINT', async () => {
      console.log('\n⏹️  Disconnecting...')
      await channel.close()
      await connection.close()
      process.exit(0)
    })
  } catch (err) {
    console.error('❌ Consumer error:', err)
    process.exit(1)
  }
}

start()
