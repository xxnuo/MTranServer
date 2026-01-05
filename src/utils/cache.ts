import crypto from 'crypto';
import { LRUCache } from 'lru-cache';
import { getConfig } from '@/config/index.js';

const config = getConfig();

const cache = new LRUCache({
  max: config.cacheSize || 200,
});

function getCacheKey(...args: any[]): string {
  let keyStr = args.join('_');
  keyStr += `_${keyStr.length}`;
  return crypto.createHash('sha1').update(keyStr).digest('hex');
}

export function readCacheTranslateWithPivot(args: any[]): string | null {

  if (config.cacheSize === 0) {
    return null;
  }

  const key = getCacheKey(...args);

  const cacheEntry = cache.get(key);
  if (cacheEntry) {
    return cacheEntry as string;
  }
  
  return null;
}

export function writeCacheTranslateWithPivot(result: string, args: any[]): void {

  if (config.cacheSize === 0) {
    return;
  }

  const key = getCacheKey(...args);

  cache.set(key, result);
}
