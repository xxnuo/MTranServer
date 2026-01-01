import { detectLanguage } from '../src/services/detector.ts';

async function run() {
  console.log('Starting reproduction...');

  // Sizes: 1KB, 10KB, 100KB, 500KB, 1MB, 2MB, 5MB, 10MB
  const sizes = [1024, 10 * 1024, 100 * 1024, 500 * 1024, 1024 * 1024, 2 * 1024 * 1024, 5 * 1024 * 1024, 10 * 1024 * 1024];

  for (const size of sizes) {
    console.log(`Testing size: ${size}`);
    // Create a mixed text to actually trigger some detection logic
    const text = 'Hello world '.repeat(Math.floor(size / 12)) + 'Bonjour ' + '你好'; 
    try {
      const lang = await detectLanguage(text);
      console.log(`Size ${size}: detected ${lang}`);
    } catch (e) {
      console.error(`Size ${size} failed:`, e);
      // Don't break immediately, see if it persists
    }
  }
}

run();
