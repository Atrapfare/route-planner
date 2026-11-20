import { fileURLToPath, URL } from 'node:url'

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [
    vue(),
  ],
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url)),
    },
  },
  // Backend port, set by node app/start (--port).
  server: {
    proxy: {
      '/api': `http://127.0.0.1:${process.env.BACKEND_PORT ?? 8080}`,
    },
  },
})
