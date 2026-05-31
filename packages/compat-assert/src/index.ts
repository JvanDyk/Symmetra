export class AssertionError extends Error {
  actual: any;
  expected: any;
  operator: string;
  constructor(message?: string, actual?: any, expected?: any, operator?: string) {
    super(message);
    this.name = 'AssertionError';
    this.actual = actual;
    this.expected = expected;
    this.operator = operator || '';
  }
}

export function ok(value: any, message?: string | Error): asserts value {
  if (!value) {
    throw new AssertionError(message?.toString() || 'Assertion failed', value, true, '==');
  }
}

export function equal(actual: any, expected: any, message?: string | Error): void {
  if (actual !== expected) {
    throw new AssertionError(message?.toString() || `${actual} != ${expected}`, actual, expected, '!==');
  }
}

export function strictEqual(actual: any, expected: any, message?: string | Error): asserts actual is typeof expected {
  equal(actual, expected, message);
}

export function notEqual(actual: any, expected: any, message?: string | Error): void {
  if (actual === expected) {
    throw new AssertionError(message?.toString() || `${actual} == ${expected}`, actual, expected, '===');
  }
}

export function strictNotEqual(actual: any, expected: any, message?: string | Error): void {
  notEqual(actual, expected, message);
}

export function deepEqual(actual: any, expected: any, message?: string | Error): void {
  if (!isDeepEqual(actual, expected)) {
    throw new AssertionError(message?.toString() || `Objects not equal`, actual, expected, 'deepEqual');
  }
}

export function notDeepEqual(actual: any, expected: any, message?: string | Error): void {
  if (isDeepEqual(actual, expected)) {
    throw new AssertionError(message?.toString() || `Objects are equal`, actual, expected, 'notDeepEqual');
  }
}

export function throws(fn: () => any, message?: string | Error): void {
  try {
    fn();
    throw new AssertionError(message?.toString() || 'Function did not throw');
  } catch (e) {
    if (e instanceof AssertionError) throw e;
  }
}

export function doesNotThrow(fn: () => any, message?: string | Error): void {
  try {
    fn();
  } catch (e) {
    throw new AssertionError(message?.toString() || `Function threw: ${e}`);
  }
}

function isDeepEqual(a: any, b: any): boolean {
  if (a === b) return true;
  if (a == null || b == null) return a === b;
  if (typeof a !== 'object' || typeof b !== 'object') return false;
  const keysA = Object.keys(a);
  const keysB = Object.keys(b);
  if (keysA.length !== keysB.length) return false;
  for (const key of keysA) {
    if (!keysB.includes(key)) return false;
    if (!isDeepEqual(a[key], b[key])) return false;
  }
  return true;
}

export default { ok, equal, strictEqual, notEqual, strictNotEqual, deepEqual, notDeepEqual, throws, doesNotThrow, AssertionError };
