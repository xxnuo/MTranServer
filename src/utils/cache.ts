import crypto from 'crypto';
import { LRUCache } from 'lru-cache';
import { getConfig } from '@/config/index.js';

const config = getConfig();

// Initialize cache with a safe fallback size if disabled.
// Actual enabling/disabling is handled in the read/write functions.
const cache = new LRUCache<string, string>({
  max: config.cacheSize > 0 ? config.cacheSize : 1,
});

/**
 * Generates a collision-resistant cache key from arguments.
 * Uses a null character separator to distinguish boundaries.
 */
function getCacheKey(args: any[]): string {
  const hash = crypto.createHash('sha1');
  for (const arg of args) {
    hash.update(String(arg));
    // Use a null character as a separator to prevent collisions
    // e.g. ["ab", "c"] vs ["a", "bc"]
    hash.update('\0');
  }
  return hash.digest('hex');
}

export function readCache(args: any[]): string | null {
  if (config.cacheSize <= 0) {
    return null;
  }

  const key = getCacheKey(args);
  return cache.get(key) || null;
}

export function writeCache(result: string, args: any[]): void {
  if (config.cacheSize <= 0) {
    return;
  }

  const key = getCacheKey(args);
  cache.set(key, result);
}