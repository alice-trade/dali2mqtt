/*
 * Copyright (c) 2026 Alice-Trade Inc.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  base: './',
  build: {
    outDir: 'dist',
    assetsDir: '',
    emptyOutDir: true,
    rollupOptions: {
      output: {
        entryFileNames: `[hash].js`,
        chunkFileNames: `[hash].js`,
        assetFileNames: `[hash].[ext]`
      }
    }
  }
})