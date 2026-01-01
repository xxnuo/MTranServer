import { MTran } from 'mtranserver';

async function main() {
  console.log('--- MTranServer Library Usage Example ---');

  // 1. Initialize MTran instance
  // You can pass configuration options here.
  const mtran = new MTran();

  try {
    // 2. Initialize (loads records, prepares environment)
    console.log('Initializing MTran...');
    await mtran.init();

    const fromLang = 'en';
    const toLang = 'zh';
    const text = 'Hello world! This is a library test.';

    // 3. (Optional) Ensure the model is downloaded.
    // .translate() will error if the model is missing in offline mode,
    // or attempt to download it if online (default).
    // Calling downloadModel explicitly is good for pre-warming.
    console.log(`Checking model for ${fromLang} -> ${toLang}...`);
    await mtran.downloadModel(fromLang, toLang);

    // 4. Perform Translation
    console.log(`
Translating: "${text}"`);
    const start = performance.now();

    const result = await mtran.translate(fromLang, toLang, text);

    const end = performance.now();
    console.log(`Translation Result: "${result}"`);
    console.log(`Time taken: ${(end - start).toFixed(2)}ms`);

    // 5. Language Detection
    console.log('\nDetecting language for: "Bonjour tout le monde"');
    const detected = await mtran.detect("Bonjour tout le monde");
    console.log(`Detected: ${detected}`);

  } catch (error) {
    console.error('An error occurred:', error);
  } finally {
    // 6. Cleanup
    // Important: Stops background worker threads to allow process to exit cleanly
    console.log('\nCleaning up...');
    await mtran.close();
  }
}

main();