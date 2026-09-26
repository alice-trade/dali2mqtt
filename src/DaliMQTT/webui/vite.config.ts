/*
 * Copyright (c) 2026 Alice-Trade Inc.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { compression } from 'vite-plugin-compression2'

export default defineConfig({
  plugins: [
    vue(),
    compression({
      algorithms: ['gzip'],
      threshold: 0,
      deleteOriginalAssets: false
    })
  ],
  base: '/',
  build: {
    outDir: 'dist',
    assetsDir: '',
    emptyOutDir: true,
    rollupOptions: {
      output: {
        inlineDynamicImports: true,
        entryFileNames: `app.js`,
        chunkFileNames: `app.js`,
        assetFileNames: `app.[ext]`
      }
    }
  }
})