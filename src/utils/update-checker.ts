import { VERSION } from '@/version/index.js';
import * as logger from '@/logger/index.js';

const RELEASES_URL = 'https://github.com/xxnuo/MTranServer/releases';

function compareVersions(current: string, latest: string): number {
  const parseCurrent = current.replace(/^v/, '').split('.').map(Number);
  const parseLatest = latest.replace(/^v/, '').split('.').map(Number);
  for (let i = 0; i < Math.max(parseCurrent.length, parseLatest.length); i++) {
    const a = parseCurrent[i] || 0;
    const b = parseLatest[i] || 0;
    if (a < b) return -1;
    if (a > b) return 1;
  }
  return 0;
}

export async function checkForUpdate(): Promise<void> {
  try {
    const response = await fetch(RELEASES_URL, {
      headers: { 'Accept': 'text/html' },
      redirect: 'follow',
    });
    if (!response.ok) return;

    const html = await response.text();
    const match = html.match(/\/xxnuo\/MTranServer\/releases\/tag\/v?([\d.]+)/);
    if (!match) return;

    const latestVersion = match[1];
    const cmp = compareVersions(VERSION, latestVersion);
    if (cmp < 0) {
      logger.important(`New version available: v${latestVersion} (current: v${VERSION})`);
      logger.important(`Download from: ${RELEASES_URL}`);
    } else if (cmp > 0) {
      logger.important(`You are using a newer version: v${VERSION} (latest release: v${latestVersion})`);
    } else {
      logger.info(`No update available. You are using the latest version: v${VERSION}`);
    }
  } catch {
  }
}
