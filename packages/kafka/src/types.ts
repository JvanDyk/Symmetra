import type {
  Kafka,
  Producer,
  Consumer,
  Admin,
  KafkaConfig,
  ConsumerConfig,
  ProducerConfig,
} from 'kafkajs'
import type { SymmifyInstance } from '@symmetra/symmify'

export interface KafkaPluginOptions {
  kafka: KafkaConfig
  producer?: boolean | ProducerConfig
  consumer?: ConsumerConfig
  admin?: boolean
}

export interface KafkaDecorator {
  kafka: Kafka
  producer: Producer | null
  consumer: Consumer | null
  admin: Admin | null
}

declare module '@symmetra/symmify' {
  interface SymmifyInstance {
    kafka: KafkaDecorator
  }
}
