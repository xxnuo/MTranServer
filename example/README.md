# MTranServer Usage Examples

This directory contains examples of how to use `mtranserver` programmatically in your Node.js/Bun applications.

## Prerequisites

Ensure you have installed the library:

```bash
bun install mtranserver@latest
```

## Running the Example

You can run the example using `bun`:

```bash
bun example/usage.ts
```

## Code Explanation

The key component is the `MTran` class:

```typescript
import { MTran } from 'mtranserver';

// 1. Create instance
const mtran = new MTran({
  modelDir: './models' // Directory to store translation models
});

// 2. Initialize
await mtran.init();

// 3. Translate
const result = await mtran.translate('en', 'es', 'Hello world');
console.log(result); // "Hola mundo"

// 4. Cleanup
await mtran.close();
```

## Configuration

The `MTran` constructor accepts a config object with the following optional properties:

- `modelDir`: Path to store downloaded models.
- `configDir`: Path to store configuration files (like `records.json`).
- `logLevel`: Logging verbosity ('debug', 'info', 'warn', 'error').
- `enableOfflineMode`: Use previously prepared translation files without downloading new ones. Prepare each required language direction online first, and keep both `configDir` and `modelDir` when moving files.
- `workerIdleTimeout`: Release an idle translation engine after this many seconds (default: 60).
- `cacheSize`: Number of recent translation results to keep (default: 1000; set to 0 to disable).

The first translation in each language direction downloads and prepares its files. This may also be needed after an upgrade. Wait for preparation to finish; later translations reuse the prepared files.
