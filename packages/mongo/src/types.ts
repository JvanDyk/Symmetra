import type { MongoClient, Db, MongoClientOptions } from 'mongodb'
import type { SymmifyInstance } from '@symmetra/symmify'

export interface MongoPluginOptions {
  url: string
  database: string
  clientOptions?: MongoClientOptions
}

declare module '@symmetra/symmify' {
  interface SymmifyInstance {
    mongo: Db
    mongoClient: MongoClient
  }
}
