import typescript from '@rollup/plugin-typescript';
import resolve from '@rollup/plugin-node-resolve';
import commonjs from '@rollup/plugin-commonjs';
import json from '@rollup/plugin-json';
import terser from '@rollup/plugin-terser';
import nodePolyfills from 'rollup-plugin-polyfill-node';
import pkg from './package.json';

export default [
    {
        input: 'src/index.ts',
        output: [
            {
                file: pkg.main,
                format: 'cjs',
                sourcemap: true,
                exports: 'named'
            },
            {
                file: pkg.module,
                format: 'es',
                sourcemap: true
            }
        ],
        plugins: [
            json(),
            resolve(),
            commonjs(),
            typescript({
                tsconfig: './tsconfig.json',
                declaration: true,
                declarationDir: './dist'
            })
        ],
        external: ['events', 'mqtt']
    },
    {
        input: 'src/index.ts',
        output: {
            name: 'dalimqx',
            file: 'dist/dalimqx.browser.min.js',
            format: 'umd',
            sourcemap: true,

        },
        plugins: [
            nodePolyfills(),
            json(),
            resolve({ browser: true }),
            commonjs(),
            typescript({ tsconfig: './tsconfig.json', declaration: false, declarationMap: false }),
            terser()
        ]
    }
];