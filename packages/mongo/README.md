# @symmetra/mongo

MongoDB plugin for `@symmetra/symmify`.

Provides a clean, type-safe integration of MongoDB with the Fastify-compatible Symmify HTTP framework via the plugin/decorator system.

## Installation

```bash
npm install @symmetra/mongo @symmetra/symmify mongodb
```

## Usage

Register the plugin with your Symmify instance:

```typescript
import symmify from '@symmetra/symmify'
import mongo from '@symmetra/mongo'

const app = symmify()

await app.register(mongo, {
  url: 'mongodb://localhost:27017',
  database: 'myapp',
})

app.get('/users', async (req, reply) => {
  const users = await req.server.mongo.collection('users').find({}).toArray()
  reply.send(users)
})

app.post('/users', async (req, reply) => {
  const result = await req.server.mongo.collection('users').insertOne(req.body)
  reply.code(201).send(result)
})

await app.listen({ port: 3000 })
```

## API

### Plugin Options

```typescript
interface MongoPluginOptions {
  url: string                          // MongoDB connection string
  database: string                     // Database name
  clientOptions?: MongoClientOptions   // Optional MongoDB client options
}
```

### Decorators

Once registered, the plugin decorates your Symmify instance with:

- **`req.server.mongo: Db`** — The MongoDB database instance. Use `db.collection(name)` to access collections and execute queries.
- **`req.server.mongoClient: MongoClient`** — The underlying MongoDB client (rarely needed).

## Example: Full CRUD API

```typescript
import symmify from '@symmetra/symmify'
import mongo from '@symmetra/mongo'
import { ObjectId } from 'mongodb'
import { z } from 'zod'

const app = symmify()

await app.register(mongo, {
  url: 'mongodb://localhost:27017',
  database: 'blog',
})

// GET all posts
app.get('/posts', async (req, reply) => {
  const posts = await req.server.mongo.collection('posts').find({}).toArray()
  reply.send(posts)
})

// GET post by ID
app.get('/posts/:id', async (req, reply) => {
  const post = await req.server.mongo.collection('posts').findOne({
    _id: new ObjectId(req.params.id),
  })
  if (!post) return reply.code(404).send({ error: 'Not found' })
  reply.send(post)
})

// POST new post
app.post(
  '/posts',
  {
    schema: {
      body: z.object({
        title: z.string(),
        content: z.string(),
      }),
    },
  },
  async (req, reply) => {
    const result = await req.server.mongo.collection('posts').insertOne({
      ...req.body,
      createdAt: new Date(),
    })
    reply.code(201).send(result)
  }
)

// PUT update post
app.put('/posts/:id', async (req, reply) => {
  const result = await req.server.mongo.collection('posts').updateOne(
    { _id: new ObjectId(req.params.id) },
    { $set: req.body }
  )
  reply.send(result)
})

// DELETE post
app.delete('/posts/:id', async (req, reply) => {
  await req.server.mongo.collection('posts').deleteOne({
    _id: new ObjectId(req.params.id),
  })
  reply.code(204).send()
})

await app.listen({ port: 3000 })
```

## Lifecycle

The plugin automatically handles MongoDB connection lifecycle:

- **On register**: Connects to MongoDB and exposes the database instance
- **On close**: Closes the MongoDB client connection gracefully

## Type Safety

The plugin is fully typed. Your IDE will provide autocomplete for:

```typescript
req.server.mongo    // type: Db (from mongodb package)
```

## License

MIT
