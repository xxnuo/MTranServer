#!/usr/bin/env node

import { run } from './server/index.js';

run().catch(error => {
  console.error('Failed to start server:', error);
  process.exit(1);
});
