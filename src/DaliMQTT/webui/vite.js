import { build } from 'vite';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const isWatch = process.argv.includes('--watch');

async function run() {
    try {
        await build({
            root: __dirname,
            configFile: path.resolve(__dirname, 'vite.config.ts'),
            build: {
                watch: isWatch ? {} : null,
            },
            logLevel: 'info',
        });
    } catch (error) {
        process.exit(1);
    }
}

run();