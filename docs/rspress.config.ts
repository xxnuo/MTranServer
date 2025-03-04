import * as path from 'node:path';
import { defineConfig } from 'rspress/config';

export default defineConfig({
  root: path.join(__dirname, 'docs'),
  title: 'MTranServer',
  icon: '/icon-min.png',
  logo: {
    light: '/icon-banner-light.png',
    dark: '/icon-banner-dark.png',
  },
  themeConfig: {
    socialLinks: [
      {
        icon: 'github',
        mode: 'link',
        content: 'https://github.com/xxnuo/MTranServer',
      },
    ],
  },
});
