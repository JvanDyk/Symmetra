/**
 * Kafka Consumer Example with @symmetra/symmify
 *
 * Standalone consumer that processes events from Kafka topics.
 * Run this with: npx tsx consumer.ts
 */

import kafka from '@symmetra/kafka'
import { Kafka } from 'kafkajs'

const kafkaInstance = new Kafka({
  clientId: 'consumer-app',
  brokers: (process.env.KAFKA_BROKERS || 'localhost:9092').split(','),
  retry: {
    initialRetryTime: 100,
    retries: 8,
  },
})

const consumer = kafkaInstance.consumer({ groupId: 'event-processing-group' })

// ============================================================================
// Subscribe and process events
// ============================================================================

async function start() {
  await consumer.connect()
  console.log('✅ Connected to Kafka')

  // Subscribe to topics
  await consumer.subscribe({
    topics: ['user-events', 'order-events'],
    fromBeginning: false,
  })
  console.log('✅ Subscribed to: user-events, order-events')

  // Process messages
  await consumer.run({
    eachMessage: async ({ topic, partition, message }) => {
      try {
        const event = JSON.parse(message.value?.toString() || '{}')
        const correlationId = message.headers?.['correlation-id']?.toString()

        console.log(`
📨 [${topic}] Message received
   Partition: ${partition}
   Offset: ${message.offset}
   Key: ${message.key?.toString()}
   Correlation ID: ${correlationId}
   Event Type: ${event.type}
   Timestamp: ${event.timestamp}
   Data: ${JSON.stringify(event.data || event, null, 2)}
`)

        // Process based on event type
        if (event.type?.startsWith('user.')) {
          processUserEvent(event)
        } else if (event.type?.startsWith('order.')) {
          processOrderEvent(event)
        }
      } catch (err) {
        console.error('❌ Error processing message:', err)
      }
    },
  })
}

function processUserEvent(event: any) {
  switch (event.type) {
    case 'user.created':
      console.log(`👤 User created: ${event.userId}`)
      break
    case 'user.updated':
      console.log(`👤 User updated: ${event.userId}`)
      break
    case 'user.deleted':
      console.log(`👤 User deleted: ${event.userId}`)
      break
  }
}

function processOrderEvent(event: any) {
  switch (event.type) {
    case 'order.created':
      console.log(`📦 Order created: ${event.orderId} ($${event.amount})`)
      break
    case 'order.processing':
      console.log(`📦 Order processing: ${event.orderId}`)
      break
    case 'order.completed':
      console.log(`✅ Order completed: ${event.orderId}`)
      break
    case 'order.cancelled':
      console.log(`❌ Order cancelled: ${event.orderId}`)
      break
  }
}

// Handle graceful shutdown
process.on('SIGINT', async () => {
  console.log('\n⏹️  Disconnecting from Kafka...')
  await consumer.disconnect()
  process.exit(0)
})

process.on('SIGTERM', async () => {
  console.log('\n⏹️  Disconnecting from Kafka...')
  await consumer.disconnect()
  process.exit(0)
})

// Start consumer
start().catch((err) => {
  console.error('❌ Consumer error:', err)
  process.exit(1)
})

console.log(`
✅ Kafka Consumer started
   Group: event-processing-group
   Topics: user-events, order-events

💡 Waiting for events...
   Run the producer to send events: npm run dev
`)
