import { app } from 'electron';
import { focusMainWindow, startDesktop } from './desktop/app.js';

const gotTheLock = app.requestSingleInstanceLock();

if (!gotTheLock) {
  app.quit();
} else {
  app.on('second-instance', () => {
    focusMainWindow();
  });

  app.whenReady().then(() => {
    startDesktop();
  });
}
