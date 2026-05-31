// Node.js os module stubs

export function platform(): string {
  return 'linux';  // GPU runtime is Linux-based
}

export function arch(): string {
  return 'x64';  // Assume x64 GPU architecture
}

export function cpus(): Array<{ model: string; speed: number; times: any }> {
  return [];  // GPU runtime doesn't expose CPU info
}

export function totalmem(): number {
  return 0;  // GPU memory not accessible via os module
}

export function freemem(): number {
  return 0;
}

export function homedir(): string {
  return '/home/gpu';
}

export function hostname(): string {
  return 'symmetra-gpu';
}

export function type(): string {
  return 'Linux';
}

export function release(): string {
  return '6.0.0';
}

export function uptime(): number {
  return 0;
}

export function loadavg(): [number, number, number] {
  return [0, 0, 0];
}

export function userInfo(options?: { encoding?: string }): any {
  return {
    uid: -1,
    gid: -1,
    username: 'gpu',
    homedir: '/home/gpu',
    shell: '/bin/sh'
  };
}

export function networkInterfaces(): Record<string, any[]> {
  return {};
}

export const EOL = '\n';
export const devNull = '/dev/null';
