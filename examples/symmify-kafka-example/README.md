# Kafka Producer & Consumer Example

Complete working example showing Kafka event publishing and consuming with @symmetra/symmify.

## Files

- `producer.ts` — HTTP API that publishes events to Kafka
- `consumer.ts` — Standalone consumer that processes events

## Prerequisites

```bash
# Start Kafka locally (or use docker)
docker run -d \
  -e KAFKA_ZOOKEEPER_CONNECT=zookeeper:2181 \
  -e KAFKA_ADVERTISED_LISTENERS=PLAINTEXT://kafka:9092 \
  -p 9092:9092 \
  confluentinc/cp-kafka

# Or use docker-compose
docker-compose up
```

## Run

**Terminal 1 - Start producer:**
```bash
npm install
npm run dev
# Server at http://localhost:3001
```

**Terminal 2 - Start consumer:**
```bash
npm run consumer
# Waiting for events...
```

**Terminal 3 - Publish events:**
```bash
# User event
curl -X POST http://localhost:3001/events/user \
  -H "Content-Type: application/json" \
  -d '{
    "userId":"550e8400-e29b-41d4-a716-446655440000",
    "action":"created",
    "data":{"name":"Alice","email":"alice@example.com"}
  }'

# Order event
curl -X POST http://localhost:3001/events/order \
  -H "Content-Type: application/json" \
  -d '{
    "orderId":"550e8400-e29b-41d4-a716-446655440001",
    "userId":"550e8400-e29b-41d4-a716-446655440000",
    "amount":99.99,
    "status":"created"
  }'

# Batch events
curl -X POST http://localhost:3001/events/batch \
  -H "Content-Type: application/json" \
  -d '{
    "events": [
      {
        "topic": "user-events",
        "key": "user-1",
        "data": {"type": "user.updated", "name": "Bob"}
      },
      {
        "topic": "order-events",
        "key": "order-1",
        "data": {"type": "order.processing", "status": "shipped"}
      }
    ]
  }'
```

## What You'll See

**Producer (Terminal 1):**
```
✅ Server running at http://127.0.0.1:3001
```

**Consumer (Terminal 2):**
```
✅ Kafka Consumer started
   Group: event-processing-group
   Topics: user-events, order-events

💡 Waiting for events...

📨 [user-events] Message received
   Partition: 0
   Offset: 0
   Key: 550e8400-e29b-41d4-a716-446655440000
   Correlation ID: req-123abc
   Event Type: user.created
   Timestamp: 2026-05-31T12:00:00.000Z
   Data: {
     "type": "user.created",
     "userId": "550e8400-e29b-41d4-a716-446655440000",
     "data": {
       "name": "Alice",
       "email": "alice@example.com"
     },
     ...
   }

👤 User created: 550e8400-e29b-41d4-a716-446655440000
```

## Features Demonstrated

✅ **Producer API** — HTTP endpoints that publish to Kafka  
✅ **Consumer** — Standalone process that consumes and processes events  
✅ **Message routing** — Different handling based on event type  
✅ **Headers & correlation** — Trace events across systems  
✅ **Batch publishing** — Send multiple events at once  
✅ **Error handling** — Graceful handling of publish/consume errors  
✅ **Graceful shutdown** — SIGINT/SIGTERM handling  

## Type Safety

Full TypeScript support with Zod validation:
- Request validation in producer
- Type-safe event schema
- Event processing with type discrimination

## Real-world Patterns

This example shows:
- How to integrate Kafka with HTTP APIs
- Event-driven architecture basics
- Consumer group management
- Message correlation for distributed tracing
