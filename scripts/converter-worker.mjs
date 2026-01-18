import { register } from 'tsx/esm/api';
register();
await import('../src/converter/worker.ts');
