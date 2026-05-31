import { fp } from '@symmetra/symmify'
import type { PluginFn } from '@symmetra/symmify'
import * as amqp from 'amqplib'
import type {
  RabbitMQPluginOptions,
  RabbitMQDecorator,
  MessageHandler,
} from './types.js'

const rabbitmqPlugin: PluginFn<RabbitMQPluginOptions> = async (
  instance,
  opts,
) => {
  const { url, heartbeat = 60, prefetch = 0 } = opts

  const connectUrl =
    typeof url === 'string'
      ? `${url}?heartbeat=${heartbeat}`
      : { ...url, heartbeat }

  const connection = await amqp.connect(connectUrl)
  const channel = await connection.createChannel()
  if (prefetch > 0) await channel.prefetch(prefetch)

  function toBuffer(content: Buffer | object): Buffer {
    if (Buffer.isBuffer(content)) return content
    return Buffer.from(JSON.stringify(content))
  }

  const decorator: RabbitMQDecorator = {
    connection: connection as any,
    channel: channel as any,

    publish(exchange, routingKey, content) {
      return channel.publish(exchange, routingKey, toBuffer(content), {
        contentType: 'application/json',
      })
    },

    sendToQueue(queue, content) {
      return channel.sendToQueue(queue, toBuffer(content), {
        contentType: 'application/json',
      })
    },

    async subscribe(queue, handler: MessageHandler) {
      const { consumerTag } = await channel.consume(queue, (msg) => {
        if (!msg) return

        const ack = () => channel.ack(msg)
        const nack = (requeue = true) => channel.nack(msg, false, requeue)

        Promise.resolve(handler(msg, ack, nack)).catch(() => nack(false))
      })
      return consumerTag
    },

    async assertQueue(queue, queueOpts) {
      await channel.assertQueue(queue, queueOpts)
    },

    async assertExchange(exchange, type, exchangeOpts) {
      await channel.assertExchange(exchange, type, exchangeOpts)
    },
  }

  if (!instance.hasDecorator('rabbitmq')) {
    instance.decorate('rabbitmq', decorator)
  }

  instance.addHook('onClose', async (done) => {
    try {
      await channel.close()
    } catch {}
    try {
      await connection.close()
    } catch {}
    done()
  })
}

export default fp(rabbitmqPlugin, { name: 'symmetra-rabbitmq' })
