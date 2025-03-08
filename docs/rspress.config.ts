import * as path from "node:path";
import { defineConfig } from "rspress/config";

export default defineConfig({
  root: path.join(__dirname, "docs"),
  title: "MTranServer",
  icon: "/icon-min.png",
  logo: {
    light: "/icon-banner-light.png",
    dark: "/icon-banner-dark.png",
  },
  lang: "zh",
  themeConfig: {
    socialLinks: [
      {
        icon: "github",
        mode: "link",
        content: "https://github.com/xxnuo/MTranServer",
      },
      {
        icon: "x",
        mode: "link",
        content: "https://x.com/realxxnuo",
      },
    ],
    locales: [
      {
        lang: "en",
        outlineTitle: "ON THIS PAGE",
        label: "English",
      },
      {
        lang: "zh",
        outlineTitle: "大纲",
        label: "简体中文",
      },
    ],
  },
  locales: [
    {
      lang: "en",
      label: "English",
      title: "MTranServer",
      description: "Blazingly Fast End-to-End Translation Server",
    },
    {
      lang: "zh",
      label: "简体中文",
      title: "MTranServer",
      description: "低占用速度快可私有部署的自由版 Google 翻译",
    },
  ],
});
