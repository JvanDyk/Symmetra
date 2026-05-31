let timerId = 0;
const timers = new Map<number, any>();

export function setTimeout(callback: () => void, delay: number): number {
  const id = ++timerId;
  timers.set(id, { callback, delay, type: 'timeout' });
  return id;
}

export function setInterval(callback: () => void, delay: number): number {
  const id = ++timerId;
  timers.set(id, { callback, delay, type: 'interval' });
  return id;
}

export function setImmediate(callback: () => void): number {
  const id = ++timerId;
  timers.set(id, { callback, delay: 0, type: 'immediate' });
  return id;
}

export function clearTimeout(id: number): void {
  timers.delete(id);
}

export function clearInterval(id: number): void {
  timers.delete(id);
}

export function clearImmediate(id: number): void {
  timers.delete(id);
}

// Process timers
export const process = {
  _getActiveHandles: () => [],
  _getActiveRequests: () => [],
  nextTick: (callback: () => void) => setTimeout(callback, 0),
  hrtime: (time?: [number, number]): [number, number] => [0, 0],
  cpuUsage: () => ({ user: 0, system: 0 }),
  memoryUsage: () => ({ rss: 0, heapTotal: 0, heapUsed: 0, external: 0, arrayBuffers: 0 })
};

export default { setTimeout, setInterval, setImmediate, clearTimeout, clearInterval, clearImmediate, process };
