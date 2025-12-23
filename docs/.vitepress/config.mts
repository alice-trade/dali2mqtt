import { defineConfig } from 'vitepress'

export default defineConfig({
  title: "DaliMQTT",
  description: "DALI to MQTT Bridge Firmware for ESP platform",
    themeConfig: {
        nav: [
            { text: 'Home', link: '/' },
            { text: 'Guide', link: '/guide/getting-started' },
            { text: 'API Reference', link: '/api/mqtt' },
            { text: 'Releases', link: 'https://github.com/youruser/DaliMQTT/releases' }
        ],

        sidebar: [
            {
                text: 'Introduction',
                items: [
                    { text: 'Getting Started', link: '/guide/getting-started' },
                    { text: 'Hardware & Wiring', link: '/guide/wiring' },
                    { text: 'Flashing Firmware', link: '/guide/flashing' }
                ]
            },
            {
                text: 'Configuration',
                items: [
                    { text: 'Network & MQTT', link: '/guide/configuration' },
                    { text: 'DALI Addressing', link: '/guide/dali-setup' },
                    { text: 'Home Assistant', link: '/guide/home-assistant' }
                ]
            },
            {
                text: 'Reference',
                items: [
                    { text: 'MQTT Topics', link: '/api/mqtt' },
                    { text: 'HTTP API', link: '/api/http' },
                    { text: 'Troubleshooting', link: '/guide/troubleshooting' }
                ]
            }
        ],

        socialLinks: [
            { icon: 'github', link: 'https://github.com/youruser/DaliMQTT' }
        ],

        footer: {
            message: 'Released under GNU GPL License.',
            copyright: 'Copyright © 2025 DaliMQTT Project'
        }
    }
})
