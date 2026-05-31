/**
 * MongoDB CRUD Example with @symmetra/symmify
 *
 * Complete working example demonstrating:
 * - CREATE (POST)
 * - READ (GET)
 * - UPDATE (PUT)
 * - DELETE (DELETE)
 */

import symmify from '@symmetra/symmify'
import mongo from '@symmetra/mongo'
import { z } from 'zod'
import { ObjectId } from 'mongodb'

// Schema validation with Zod
const UserSchema = z.object({
  name: z.string().min(1),
  email: z.string().email(),
  age: z.number().int().min(0).max(150),
})

type User = z.infer<typeof UserSchema>

// Initialize app
const app = symmify({ logger: true })

// Register MongoDB plugin
await app.register(mongo, {
  url: process.env.MONGO_URL || 'mongodb://localhost:27017',
  database: 'test_db',
})

// ============================================================================
// CREATE - POST /users
// ============================================================================
app.post(
  '/users',
  {
    schema: {
      body: UserSchema,
    },
  },
  async (req, reply) => {
    const user: User = req.body

    try {
      const result = await app.mongo.collection('users').insertOne({
        ...user,
        createdAt: new Date(),
        updatedAt: new Date(),
      })

      reply.code(201).send({
        _id: result.insertedId,
        ...user,
        createdAt: new Date(),
      })
    } catch (err) {
      req.log.error(err, 'Failed to create user')
      reply.code(500).send({ error: 'Failed to create user' })
    }
  }
)

// ============================================================================
// READ ALL - GET /users
// ============================================================================
app.get('/users', async (req, reply) => {
  try {
    const users = await app.mongo
      .collection('users')
      .find({})
      .sort({ createdAt: -1 })
      .limit(100)
      .toArray()

    reply.send({
      count: users.length,
      users: users.map((u: any) => ({
        _id: u._id,
        name: u.name,
        email: u.email,
        age: u.age,
        createdAt: u.createdAt,
      })),
    })
  } catch (err) {
    req.log.error(err, 'Failed to fetch users')
    reply.code(500).send({ error: 'Failed to fetch users' })
  }
})

// ============================================================================
// READ ONE - GET /users/:id
// ============================================================================
app.get(
  '/users/:id',
  {
    schema: {
      params: z.object({
        id: z.string().regex(/^[0-9a-f]{24}$/i),
      }),
    },
  },
  async (req, reply) => {
    try {
      const user = await app.mongo.collection('users').findOne({
        _id: new ObjectId(req.params.id),
      })

      if (!user) {
        return reply.code(404).send({ error: 'User not found' })
      }

      reply.send(user)
    } catch (err) {
      req.log.error(err, 'Failed to fetch user')
      reply.code(500).send({ error: 'Failed to fetch user' })
    }
  }
)

// ============================================================================
// UPDATE - PUT /users/:id
// ============================================================================
app.put(
  '/users/:id',
  {
    schema: {
      params: z.object({
        id: z.string().regex(/^[0-9a-f]{24}$/i),
      }),
      body: UserSchema.partial(),
    },
  },
  async (req, reply) => {
    try {
      const result = await app.mongo.collection('users').findOneAndUpdate(
        { _id: new ObjectId(req.params.id) },
        {
          $set: {
            ...req.body,
            updatedAt: new Date(),
          },
        },
        { returnDocument: 'after' }
      )

      if (!result.value) {
        return reply.code(404).send({ error: 'User not found' })
      }

      reply.send(result.value)
    } catch (err) {
      req.log.error(err, 'Failed to update user')
      reply.code(500).send({ error: 'Failed to update user' })
    }
  }
)

// ============================================================================
// DELETE - DELETE /users/:id
// ============================================================================
app.delete(
  '/users/:id',
  {
    schema: {
      params: z.object({
        id: z.string().regex(/^[0-9a-f]{24}$/i),
      }),
    },
  },
  async (req, reply) => {
    try {
      const result = await app.mongo
        .collection('users')
        .deleteOne({ _id: new ObjectId(req.params.id) })

      if (result.deletedCount === 0) {
        return reply.code(404).send({ error: 'User not found' })
      }

      reply.code(204).send()
    } catch (err) {
      req.log.error(err, 'Failed to delete user')
      reply.code(500).send({ error: 'Failed to delete user' })
    }
  }
)

// ============================================================================
// Health check
// ============================================================================
app.get('/health', async (req, reply) => {
  try {
    await app.mongo.admin().ping()
    reply.send({ status: 'ok', mongodb: '✅' })
  } catch (err) {
    reply.code(503).send({ status: 'error', mongodb: '❌' })
  }
})

// ============================================================================
// Start server
// ============================================================================
const port = parseInt(process.env.PORT || '3000')
const url = await app.listen({ port, host: '127.0.0.1' })

console.log(`
✅ Server running at ${url}

📝 CRUD Endpoints:
  POST   ${url}/users           (create user)
  GET    ${url}/users           (list users)
  GET    ${url}/users/:id       (get user)
  PUT    ${url}/users/:id       (update user)
  DELETE ${url}/users/:id       (delete user)
  GET    ${url}/health          (health check)

🧪 Test it:
  curl -X POST ${url}/users \\
    -H "Content-Type: application/json" \\
    -d '{"name":"Alice","email":"alice@example.com","age":30}'
`)

// Graceful shutdown
process.on('SIGINT', async () => {
  console.log('\n⏹️  Shutting down...')
  await app.close()
  process.exit(0)
})
