import type { Connection, Channel, Options, ConsumeMessage } from 'amqplib'
import type { SymmifyInstance } from '@symmetra/symmify'

export interface RabbitMQPluginOptions {
  url: string | Options.Connect
  heartbeat?: number
  prefetch?: number
}

export type MessageHandler = (
  msg: ConsumeMessage,
  ack: () => void,
  nack: (requeue?: boolean) => void,
) => void | Promise<void>

export interface RabbitMQDecorator {
  connection: Connection
  channel: Channel
  publish(exchange: string, routingKey: string, content: Buffer | object): boolean
  sendToQueue(queue: string, content: Buffer | object): boolean
  subscribe(queue: string, handler: MessageHandler): Promise<string>
  assertQueue(queue: string, opts?: Options.AssertQueue): Promise<void>
  assertExchange(
    exchange: string,
    type: string,
    opts?: Options.AssertExchange,
  ): Promise<void>
}

declare module '@symmetra/symmify' {
  interface SymmifyInstance {
    rabbitmq: RabbitMQDecorator
  }
}
