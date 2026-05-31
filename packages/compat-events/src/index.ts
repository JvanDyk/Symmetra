// Node.js events module compatibility layer

export type EventListener = (...args: any[]) => void | Promise<void>;

export interface EventEmitterOptions {
  captureRejections?: boolean;
}

export class EventEmitter {
  private _events: Map<string | symbol, EventListener[]> = new Map();
  private _maxListeners: number = 10;
  private _captureRejections: boolean = false;

  constructor(options?: EventEmitterOptions) {
    if (options?.captureRejections) {
      this._captureRejections = true;
    }
  }

  // Register listener
  on(eventName: string | symbol, listener: EventListener): this {
    if (typeof listener !== 'function') {
      throw new TypeError('Listener must be a function');
    }

    if (!this._events.has(eventName)) {
      this._events.set(eventName, []);
    }

    const listeners = this._events.get(eventName)!;
    listeners.push(listener);

    // Check max listeners
    if (listeners.length > this._maxListeners && this._maxListeners > 0) {
      console.warn(
        `MaxListenersExceededWarning: ${listeners.length} ${String(eventName)} listeners added. ` +
        `Max is ${this._maxListeners}. Use emitter.setMaxListeners() to increase limit`
      );
    }

    return this;
  }

  // One-time listener
  once(eventName: string | symbol, listener: EventListener): this {
    const wrapped = (...args: any[]) => {
      this.removeListener(eventName, wrapped);
      return listener(...args);
    };
    return this.on(eventName, wrapped);
  }

  // Remove listener
  off(eventName: string | symbol, listener: EventListener): this {
    return this.removeListener(eventName, listener);
  }

  removeListener(eventName: string | symbol, listener: EventListener): this {
    const listeners = this._events.get(eventName);
    if (!listeners) return this;

    const idx = listeners.indexOf(listener);
    if (idx >= 0) {
      listeners.splice(idx, 1);
    }

    if (listeners.length === 0) {
      this._events.delete(eventName);
    }

    return this;
  }

  // Remove all listeners
  removeAllListeners(eventName?: string | symbol): this {
    if (eventName) {
      this._events.delete(eventName);
    } else {
      this._events.clear();
    }
    return this;
  }

  // Emit event
  emit(eventName: string | symbol, ...args: any[]): boolean {
    const listeners = this._events.get(eventName);
    if (!listeners) return false;

    for (const listener of listeners) {
      try {
        listener(...args);
      } catch (error) {
        if (this._captureRejections) {
          this.emit('error', error);
        } else {
          throw error;
        }
      }
    }

    return true;
  }

  // Add listeners at beginning
  prependListener(eventName: string | symbol, listener: EventListener): this {
    if (typeof listener !== 'function') {
      throw new TypeError('Listener must be a function');
    }

    if (!this._events.has(eventName)) {
      this._events.set(eventName, []);
    }

    const listeners = this._events.get(eventName)!;
    listeners.unshift(listener);

    return this;
  }

  prependOnceListener(eventName: string | symbol, listener: EventListener): this {
    const wrapped = (...args: any[]) => {
      this.removeListener(eventName, wrapped);
      return listener(...args);
    };
    return this.prependListener(eventName, wrapped);
  }

  // List listeners
  listeners(eventName: string | symbol): EventListener[] {
    return (this._events.get(eventName) || []).slice();
  }

  rawListeners(eventName: string | symbol): EventListener[] {
    return this.listeners(eventName);
  }

  listenerCount(eventName?: string | symbol): number {
    if (eventName) {
      return (this._events.get(eventName) || []).length;
    }
    return this._events.size;
  }

  // Event names
  eventNames(): (string | symbol)[] {
    return Array.from(this._events.keys());
  }

  // Max listeners
  setMaxListeners(n: number): this {
    if (n < 0 || !Number.isInteger(n)) {
      throw new RangeError('maxListeners must be a non-negative integer');
    }
    this._maxListeners = n;
    return this;
  }

  getMaxListeners(): number {
    return this._maxListeners;
  }

  // Static methods
  static defaultMaxListeners = 10;
  static setMaxListeners(n: number): typeof EventEmitter {
    EventEmitter.defaultMaxListeners = n;
    return EventEmitter;
  }

  static getMaxListeners(): number {
    return EventEmitter.defaultMaxListeners;
  }

  // Async iteration (for newer Node.js versions)
  async *[Symbol.asyncIterator](): AsyncGenerator<any[], void> {
    const eventName = 'data';
    const queue: any[][] = [];
    let done = false;

    this.on(eventName, (...args: any[]) => {
      queue.push(args);
    });

    this.once('end', () => {
      done = true;
    });

    while (!done || queue.length > 0) {
      if (queue.length > 0) {
        yield queue.shift()!;
      } else if (!done) {
        await new Promise(resolve => setTimeout(resolve, 10));
      }
    }
  }
}

// Default export
export default EventEmitter;
