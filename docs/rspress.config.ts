import * as path from 'node:path';
import { defineConfig } from 'rspress/config';

export default defineConfig({
  root: path.join(__dirname, 'docs'),
  title: 'MTranServer',
  icon: '/icon-min.png',
  logo: {
    light: '/icon-min.png',
    dark: '/icon-min.png',
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
