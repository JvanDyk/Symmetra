# RabbitMQ Producer & Consumer Example

Complete working example showing RabbitMQ message queuing with @symmetra/symmify.

## Files

- `producer.ts` — HTTP API that publishes messages to RabbitMQ queues
- `consumer.ts` — Standalone consumer that processes messages

## Prerequisites

```bash
# Start RabbitMQ locally
docker run -d \
  -p 5672:5672 \
  -p 15672:15672 \
  rabbitmq:3-management

# Management UI: http://localhost:15672 (guest:guest)
```

## Run

**Terminal 1 - Start producer:**
```bash
npm install
npm run dev
# Server at http://localhost:3002
```

**Terminal 2 - Start consumer:**
```bash
npm run consumer
# Listening on email-queue, notification-queue, task-queue
```

**Terminal 3 - Send messages:**
```bash
# Send email
curl -X POST http://localhost:3002/queue/email \
  -H "Content-Type: application/json" \
  -d '{
    "to":"user@example.com",
    "subject":"Welcome!",
    "template":"welcome",
    "data":{"name":"Alice"}
  }'

# Send notification
curl -X POST http://localhost:3002/queue/notification \
  -H "Content-Type: application/json" \
  -d '{
    "userId":"550e8400-e29b-41d4-a716-446655440000",
    "type":"login",
    "message":"Welcome back!",
    "data":{"ip":"192.168.1.1"}
  }'

# Queue task
curl -X POST http://localhost:3002/queue/task \
  -H "Content-Type: application/json" \
  -d '{
    "taskId":"550e8400-e29b-41d4-a716-446655440001",
    "taskType":"export",
    "priority":"high",
    "payload":{"format":"csv","limit":1000}
  }'

# Direct queue send
curl -X POST http://localhost:3002/queue/send/email-queue \
  -H "Content-Type: application/json" \
  -d '{
    "to":"admin@example.com",
    "subject":"Alert",
    "message":"System warning"
  }'
```

## What You'll See

**Producer (Terminal 1):**
```
✅ RabbitMQ Producer running at http://127.0.0.1:3002

Queued email to user@example.com
Queued notification for user 550e8400-e29b-41d4-a716-446655440000: login
Queued task 550e8400-e29b-41d4-a716-446655440001 (export)
```

**Consumer (Terminal 2):**
```
✅ Consumers started!
   Waiting for messages...

✉️  Email Message
   To: user@example.com
   Subject: Welcome!
   Template: welcome
   Data: {"name":"Alice"}
   Queued At: 2026-05-31T12:00:00.000Z

✅ Email sent to user@example.com

🔔 Notification Message
   User: 550e8400-e29b-41d4-a716-446655440000
   Type: login
   Message: Welcome back!
   Data: {"ip":"192.168.1.1"}

✅ Notification sent to user 550e8400-e29b-41d4-a716-446655440000 (login)

⚙️  Task Message
   Task ID: 550e8400-e29b-41d4-a716-446655440001
   Type: export
   Priority: high
   Payload: {"format":"csv","limit":1000}
   Queued At: 2026-05-31T12:00:00.000Z

✅ Task completed: 550e8400-e29b-41d4-a716-446655440001
```

## Features Demonstrated

✅ **Producer API** — HTTP endpoints that queue messages  
✅ **Consumer** — Standalone process that consumes and processes messages  
✅ **Exchange types** — Direct, Topic, and Fanout exchanges  
✅ **Message acknowledgment** — ACK/NACK handling  
✅ **Error handling** — Requeue failed messages  
✅ **Prefetch** — Control message processing rate  
✅ **Type safety** — Zod validation for messages  

## Exchange Types Used

1. **Direct Exchange** (`emails`)
   - Exact routing key match
   - Used for: point-to-point messages (emails)

2. **Topic Exchange** (`notifications`)
   - Pattern-based routing (user.*)
   - Used for: publish-subscribe (notifications)

3. **Fanout Exchange** (`tasks`)
   - Broadcast to all bound queues
   - Used for: task distribution

## Real-world Patterns

This example shows:
- How to integrate RabbitMQ with HTTP APIs
- Task queueing for background processing
- Message-driven architecture
- Consumer patterns with retries
- Different exchange types for different use cases
