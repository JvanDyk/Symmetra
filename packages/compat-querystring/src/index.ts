export function stringify(obj: Record<string, any>, sep = '&', eq = '='): string {
  return Object.entries(obj).map(([k, v]) => `${encodeURIComponent(k)}${eq}${encodeURIComponent(v)}`).join(sep);
}

export function parse(str: string, sep = '&', eq = '='): Record<string, any> {
  const obj: Record<string, any> = {};
  if (!str) return obj;
  for (const pair of str.split(sep)) {
    const idx = pair.indexOf(eq);
    const k = decodeURIComponent(idx < 0 ? pair : pair.substring(0, idx));
    const v = decodeURIComponent(idx < 0 ? '' : pair.substring(idx + eq.length));
    if (obj[k]) {
      obj[k] = Array.isArray(obj[k]) ? [...obj[k], v] : [obj[k], v];
    } else {
      obj[k] = v;
    }
  }
  return obj;
}

export function escape(str: string): string {
  return str.replace(/[^A-Za-z0-9!~*'()-]/g, x => `%${x.charCodeAt(0).toString(16).toUpperCase().padStart(2, '0')}`);
}

export function unescape(str: string): string {
  return str.replace(/%[0-9A-Fa-f]{2}/g, x => String.fromCharCode(parseInt(x.substring(1), 16)));
}
