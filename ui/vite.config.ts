import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react-swc'
import path from "path"
import tailwindcss from "@tailwindcss/vite"

export default defineConfig({
  base: '/ui/',
  plugins: [react(), tailwindcss()],
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "./src"),
    },
  },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
  server: {
    proxy: {
      '/languages': {
        target: 'http://localhost:8989',
        changeOrigin: true,
      },
      '/translate': {
        target: 'http://localhost:8989',
        changeOrigin: true,
      },
    },
  },
})
