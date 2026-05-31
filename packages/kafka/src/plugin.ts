import { fp } from '@symmetra/symmify'
import type { PluginFn } from '@symmetra/symmify'
import { Kafka } from 'kafkajs'
import type { KafkaPluginOptions, KafkaDecorator } from './types.js'

const kafkaPlugin: PluginFn<KafkaPluginOptions> = async (instance, opts) => {
  const {
    kafka: kafkaConfig,
    producer: producerOpts,
    consumer: consumerOpts,
    admin: adminOpts,
  } = opts

  const kafka = new Kafka(kafkaConfig)

  const decorator: KafkaDecorator = {
    kafka,
    producer: null,
    consumer: null,
    admin: null,
  }

  if (producerOpts) {
    const producerConfig = typeof producerOpts === 'object' ? producerOpts : {}
    decorator.producer = kafka.producer(producerConfig)
    await decorator.producer.connect()
  }

  if (consumerOpts) {
    decorator.consumer = kafka.consumer(consumerOpts)
    await decorator.consumer.connect()
  }

  if (adminOpts) {
    decorator.admin = kafka.admin()
    await decorator.admin.connect()
  }

  if (!instance.hasDecorator('kafka')) {
    instance.decorate('kafka', decorator)
  }

  instance.addHook('onClose', async (done) => {
    if (decorator.consumer) await decorator.consumer.disconnect()
    if (decorator.producer) await decorator.producer.disconnect()
    if (decorator.admin) await decorator.admin.disconnect()
    done()
  })
}

export default fp(kafkaPlugin, { name: 'symmetra-kafka' })
