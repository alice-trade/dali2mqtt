import { defineConfig } from 'vitepress'
import { withMermaid } from 'vitepress-plugin-mermaid'

const MainConfig = defineConfig({
    title: "DaliMQTT",
    description: "DALI to MQTT Bridge Firmware for ESP platform",
    themeConfig: {
        nav: [
            { text: 'Home', link: '/' },
            { text: 'Guide', link: '/guide/getting-started' },
            { text: 'API Reference', link: '/api/mqtt' },
            { text: 'JS SDK', link: '/dalimqx/' },
            { text: 'Releases', link: 'https://github.com/alice-trade/dali2mqtt/releases' }
        ],

        sidebar: {
            '/guide/': [
                {
                    text: 'Introduction',
                    items: [
                        { text: 'Getting Started', link: '/guide/getting-started' },
                    ]
                },
                {
                    text: 'Configuration',
                    items: [
                        { text: 'Network & MQTT', link: '/guide/configuration' },
                        { text: 'DALI Setup', link: '/guide/dali-setup' },
                        { text: 'Home Assistant', link: '/guide/home-assistant' }
                    ]
                }
            ],
            '/api/': [
                {
                    text: 'Reference',
                    items: [
                        { text: 'MQTT Topics', link: '/api/mqtt' },
                        { text: 'HTTP API', link: '/api/http' }
                    ]
                }
            ],
            '/dalimqx/': [
                {
                    text: 'DaliMQX SDK',
                    items: [
                        { text: 'Usage Guide', link: '/dalimqx/' },
                        { text: 'API Reference', link: '/dalimqx/api-reference/' },
                    ]
                }
            ]
        },

        socialLinks: [
            { icon: 'github', link: 'https://github.com/alice-trade/dali2mqtt' }
        ],

        footer: {
            message: 'Released under GNU GPL v3 License.',
            copyright: 'Copyright © 2025 DaliMQTT Project'
        }
    }
});

const StyledConfig = withMermaid({ ...MainConfig})
export default StyledConfig

