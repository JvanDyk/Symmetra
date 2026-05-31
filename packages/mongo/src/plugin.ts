import { fp } from '@symmetra/symmify'
import type { PluginFn } from '@symmetra/symmify'
import { MongoClient } from 'mongodb'
import type { MongoPluginOptions } from './types.js'

const mongoPlugin: PluginFn<MongoPluginOptions> = async (instance, opts) => {
  const { url, database, clientOptions = {} } = opts

  const client = new MongoClient(url, clientOptions)
  await client.connect()
  const db = client.db(database)

  if (!instance.hasDecorator('mongo')) {
    instance.decorate('mongo', db)
    instance.decorate('mongoClient', client)
  }

  instance.addHook('onClose', async (done) => {
    await client.close()
    done()
  })
}

export default fp(mongoPlugin, { name: 'symmetra-mongo' })
