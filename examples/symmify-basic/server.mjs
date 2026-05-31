import symmify from '@symmetra/symmify'
import { z } from 'zod'

const app = symmify({ logger: true })

// Simple GET route
app.get('/', async (req, reply) => {
  reply.send({ message: 'Welcome to symmify!' })
})

// GET with path params and Zod validation
app.get(
  '/users/:id',
  {
    schema: {
      params: z.object({
        id: z.string().nonempty(),
      }),
    },
  },
  async (req, reply) => {
    reply.send({ user: { id: req.params.id, name: 'Alice' } })
  }
)

// POST with Zod body schema
app.post(
  '/users',
  {
    schema: {
      body: z.object({
        name: z.string().min(1),
        email: z.string().email(),
      }),
    },
  },
  async (req, reply) => {
    reply.code(201).send({
      message: 'User created',
      user: {
        id: Math.random().toString(36),
        ...req.body,
      },
    })
  }
)

// Route group with prefix
app.register(
  async (api) => {
    api.get('/list', async (req, reply) => {
      reply.send({ products: [{ id: 1, name: 'Product 1' }] })
    })

    api.post(
      '/search',
      {
        schema: {
          querystring: z.object({
            q: z.string().optional(),
          }),
        },
      },
      async (req, reply) => {
        reply.send({ results: [] })
      }
    )
  },
  { prefix: '/api/products' }
)

// Error handler
app.setErrorHandler((error, req, reply) => {
  req.log.error(error, 'Request error')
  reply
    .code(error.statusCode || 500)
    .send({
      error: error.name || 'Error',
      message: error.message,
      statusCode: error.statusCode || 500,
    })
})

// Not found handler
app.setNotFoundHandler((req, reply) => {
  reply.code(404).send({
    error: 'Not Found',
    message: `Route ${req.raw.method} ${req.url} not found`,
  })
})

// Start server
const url = await app.listen({ port: 3000, host: '127.0.0.1' })
console.log(`Server running at ${url}`)

// Graceful shutdown
process.on('SIGINT', async () => {
  await app.close()
  process.exit(0)
})
