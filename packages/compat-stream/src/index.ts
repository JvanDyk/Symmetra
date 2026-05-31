// Node.js stream module stubs (GPU runtime doesn't support streams)

import { EventEmitter } from '@symmetra/compat-events';

export class Stream extends EventEmitter {}
export class Readable extends Stream {}
export class Writable extends Stream {}
export class Transform extends Stream {}
export class Duplex extends Stream {}
export class PassThrough extends Stream {}

export function pipeline(...streams: any[]): void {
  // Stream pipelines not supported on GPU
}

export const finished = (stream: any, callback: (err?: Error | null) => void) => {
  // Streams not supported, call callback immediately
  process.nextTick(callback);
};

export default { Stream, Readable, Writable, Transform, Duplex, PassThrough, pipeline, finished };
