# ⚡ Quick Public Services Testing (Zero Setup)

Test everything using free public cloud services. **No Docker, no local setup—just sign up and test.**

---

## 5-Minute Setup

### 1. MongoDB Atlas (2 min)

**Sign up:** https://www.mongodb.com/cloud/atlas

```
1. Click "Start free"
2. Email signup (or Google/GitHub login)
3. Create cluster (default free tier is fine)
4. Wait 3 minutes for deployment
5. Click "Connect" → "Drivers" → Copy connection string
```

**Connection string format:**
```
mongodb+srv://username:password@cluster.mongodb.net/database?retryWrites=true&w=majority
```

Store in `.env`:
```env
MONGODB_URL=mongodb+srv://username:password@cluster.mongodb.net/symmetra?retryWrites=true&w=majority
```

---

### 2. Confluent Cloud Kafka (2 min)

**Sign up:** https://confluent.cloud

```
1. Click "Sign up free"
2. Email signup
3. Create cluster (default free tier works)
4. Wait for deployment
5. Go to "Cluster settings" → Copy bootstrap servers
6. Go to "API keys" → Create key → Copy credentials
```

**Connection details:**
```env
KAFKA_BROKERS=pkc-xxx.region.provider.confluent.cloud:9092
KAFKA_KEY=your-api-key
KAFKA_SECRET=your-api-secret
```

---

### 3. CloudAMQP RabbitMQ (1 min)

**Sign up:** https://www.cloudamqp.com

```
1. Click "Create Instance"
2. Select "Little Lemur" (free tier)
3. Name it "symmetra"
4. Deploy
5. Copy AMQP URL from dashboard
```

**Connection string:**
```env
RABBITMQ_URL=amqps://username:password@hostname.cloudamqp.com/vhost
```

---

## Modified Examples for Public Services

### MongoDB CRUD (Using Atlas)

**File:** `examples/symmify-mongo-crud/app.ts`

```typescript
import Symmify from '@symmetra/symmify'
import mongoPlugin from '@symmetra/mongo'
import { z } from 'zod'

const app = Symmify.create()

// MongoDB Atlas connection
await app.register(mongoPlugin, {
  url: process.env.MONGODB_URL || 'mongodb+srv://user:pass@cluster.mongodb.net/symmetra?retryWrites=true&w=majority',
  database: 'symmetra'
})

// CRUD routes (same as before)
app.post('/users', {
  schema: {
    body: z.object({
      name: z.string(),
      email: z.string().email(),
      age: z.number().optional()
    })
  },
  handler: async (req, reply) => {
    const user = await req.server.mongo.collection('users').insertOne({
      ...req.body,
      createdAt: new Date()
    })
    reply.code(201).send({ _id: user.insertedId, ...req.body })
  }
})

// ... rest of routes same as examples/symmify-mongo-crud/app.ts

await app.listen({ port: 3000, host: '0.0.0.0' })
console.log('✅ MongoDB CRUD API: http://localhost:3000')
```

**Run:**
```bash
# Set your MongoDB Atlas connection string
export MONGODB_URL="mongodb+srv://user:pass@cluster.mongodb.net/symmetra?retryWrites=true&w=majority"

cd examples/symmify-mongo-crud
npm install
npm run dev
```

---

### Kafka Producer (Using Confluent Cloud)

**File:** `examples/symmify-kafka-example/producer.ts`

```typescript
import Symmify from '@symmetra/symmify'
import kafkaPlugin from '@symmetra/kafka'

const app = Symmify.create()

// Confluent Cloud Kafka connection
await app.register(kafkaPlugin, {
  kafka: {
    clientId: 'symmetra-producer',
    brokers: [process.env.KAFKA_BROKERS || 'pkc-xxx.region.provider.confluent.cloud:9092'],
    ssl: true,
    sasl: {
      mechanism: 'plain',
      username: process.env.KAFKA_KEY || 'key',
      password: process.env.KAFKA_SECRET || 'secret'
    }
  },
  producer: true
})

app.post('/events/user', {
  handler: async (req, reply) => {
    const result = await req.server.kafka.producer.send({
      topic: 'user-events',
      messages: [
        {
          key: req.body.userId,
          value: JSON.stringify(req.body),
          headers: {
            'correlation-id': `req-${Date.now()}`
          }
        }
      ]
    })
    reply.send({ message: 'Event published', partition: result[0].partition })
  }
})

await app.listen({ port: 3001, host: '0.0.0.0' })
console.log('✅ Kafka Producer: http://localhost:3001')
```

**Run:**
```bash
# Set your Confluent Cloud credentials
export KAFKA_BROKERS="pkc-xxx.region.provider.confluent.cloud:9092"
export KAFKA_KEY="your-api-key"
export KAFKA_SECRET="your-api-secret"

cd examples/symmify-kafka-example
npm install
npm run dev
```

---

### RabbitMQ Producer (Using CloudAMQP)

**File:** `examples/symmify-rabbitmq-example/producer.ts`

```typescript
import Symmify from '@symmetra/symmify'
import rabbitmqPlugin from '@symmetra/rabbitmq'

const app = Symmify.create()

// CloudAMQP connection
await app.register(rabbitmqPlugin, {
  url: process.env.RABBITMQ_URL || 'amqps://user:pass@hostname.cloudamqp.com/vhost',
  prefetch: 1
})

app.post('/queue/email', {
  handler: async (req, reply) => {
    await req.server.rabbitmq.assertExchange('emails', 'direct', { durable: true })
    await req.server.rabbitmq.assertQueue('email-queue', { durable: true })
    await req.server.rabbitmq.publish('emails', 'send', req.body)
    reply.send({ message: 'Email queued', to: req.body.to })
  }
})

await app.listen({ port: 3002, host: '0.0.0.0' })
console.log('✅ RabbitMQ Producer: http://localhost:3002')
```

**Run:**
```bash
# Set your CloudAMQP URL
export RABBITMQ_URL="amqps://user:pass@hostname.cloudamqp.com/vhost"

cd examples/symmify-rabbitmq-example
npm install
npm run dev
```

---

## Quick Test Commands

Once running, test immediately:

```bash
# MongoDB CRUD
curl -X POST http://localhost:3000/users \
  -H "Content-Type: application/json" \
  -d '{"name":"Alice","email":"alice@example.com","age":30}'

# Kafka Producer
curl -X POST http://localhost:3001/events/user \
  -H "Content-Type: application/json" \
  -d '{"userId":"123","action":"created","data":{}}'

# RabbitMQ Producer
curl -X POST http://localhost:3002/queue/email \
  -H "Content-Type: application/json" \
  -d '{"to":"user@example.com","subject":"Test"}'
```

---

## Alternative: Use GitHub Codespaces (Still Free)

If you have a GitHub account:

1. Fork this repo
2. Click "Code" → "Codespaces" → "Create codespace"
3. Run: `./start-services.sh`
4. Everything works with Docker pre-installed

**Still zero manual setup—just a button click.**

---

## Free Tier Limits

| Service | Free Tier | Limit |
|---------|-----------|-------|
| **MongoDB Atlas** | Shared cluster | 512MB storage, 3 nodes |
| **Confluent Cloud** | Developer | 100MB, 1 cluster |
| **CloudAMQP** | Little Lemur | 1 vhost, 1 connection |
| **GitHub Codespaces** | Free | 60 hours/month |

All limits are **plenty for testing the framework**.

---

## What You Get

✅ **Real databases** — not mocks  
✅ **Real queues** — not in-memory  
✅ **Production-like** — cloud-hosted services  
✅ **Zero local setup** — no Docker, no installation  
✅ **Free forever** — no credit card required  
✅ **Same code** — examples work exactly the same  

---

## How to Use

**Choose one option:**

**Option 1: Public Services (Recommended)**
- Fastest setup (5 minutes)
- Test the framework with real services
- No local resources needed

**Option 2: GitHub Codespaces**
- Still free but with Docker
- Full docker-compose stack
- Better for comprehensive testing

---

## Example: Complete Test Flow

```bash
# 1. Sign up for MongoDB Atlas (2 min)
# 2. Get connection string
# 3. Run MongoDB example
export MONGODB_URL="mongodb+srv://..."
cd examples/symmify-mongo-crud
npm install && npm run dev

# 4. In another terminal, test it
curl -X POST http://localhost:3000/users \
  -H "Content-Type: application/json" \
  -d '{"name":"Test","email":"test@example.com","age":25}'

# 5. See response with _id
# {"_id":"507f1f77bcf86cd799439011","name":"Test",...}

# ✅ Framework works with real MongoDB!
```

Done. No Docker, no complex setup, just **framework + real services in 10 minutes.**

