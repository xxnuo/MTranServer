import { Express } from 'express';
import { cors, auth } from '@/middleware/index.js';
import {
  handleVersion,
  handleHealth,
  handleHeartbeat,
  handleLBHeartbeat,
  handleLanguages,
  handleDetectLanguage,
  handleTranslate,
  handleTranslateBatch,
  handleDeeplTranslate,
  handleImmeTranslate,
  handleGoogleCompatTranslate,
  handleGoogleTranslateSingle,
  handleKissTranslate,
  handleHcfyTranslate,
} from '@/handlers/index.js';

export function setupRoutes(app: Express, apiToken: string) {
  app.use(cors());

  app.get('/version', handleVersion);
  app.get('/health', handleHealth);
  app.get('/__heartbeat__', handleHeartbeat);
  app.get('/__lbheartbeat__', handleLBHeartbeat);

  const authMiddleware = auth(apiToken);

  app.get('/languages', authMiddleware, handleLanguages);
  app.post('/detect', authMiddleware, handleDetectLanguage);
  app.post('/translate', authMiddleware, handleTranslate);
  app.post('/translate/batch', authMiddleware, handleTranslateBatch);

  app.post('/deepl', handleDeeplTranslate(apiToken));
  app.post('/imme', handleImmeTranslate(apiToken));
  app.post('/google/language/translate/v2', authMiddleware, handleGoogleCompatTranslate);
  app.get('/google/translate_a/single', authMiddleware, handleGoogleTranslateSingle);
  app.post('/kiss', authMiddleware, handleKissTranslate);
  app.post('/hcfy', authMiddleware, handleHcfyTranslate);
}
