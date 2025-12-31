---
layout: home

hero:
  name: "DaliMQTT Bridge"
  text: "Professional DALI Control for ESP32"
  tagline: "Bridge your DALI lighting directly to MQTT and Home Assistant without expensive gateways."
  actions:
    - theme: brand
      text: Get Started
      link: /guide/getting-started
    - theme: alt
      text: View on GitHub
      link: https://github.com/alice-trade/dali2mqtt

features:
  - title: Fully Featured DALI Engine
    details: Supports addressing, groups, scenes, DT8 (Color Temp & RGB) and Input Devices (Motion/Switches).
    icon: 💡
  - title: Home Assistant Discovery
    details: Devices automatically appears in Home Assistant using MQTT Discovery. Bidirectional sync.
    icon: 🔌
  - title: Commissioning
    details: Built-in Web interface interface for commissioning, addressing, grouping, and configuration.
    icon: 🖥
  - title: JS SDK Included
    details: Comes with DaliMQX - a TypeScript library to easily build custom Node.js/TS integrations.
    icon: 📦
---

## Architecture

The **DaliMQTT** firmware runs on an ESP32, acting as a bridge between a standard DALI Bus (requires physical DALI Driver circuit) and your MQTT Broker.

```mermaid
graph LR
    HA[Home Assistant] <--> MQTT((MQTT Broker))
    JS[NodeJS / DaliMQX] <--> MQTT
    MQTT <--> ESP[ESP32 DaliMQTT]
    ESP <--> DALI[DALI Bus]
    DALI <--> L1((Light 1))
    DALI <--> L2((Light 2))
    DALI <--> S1((Switch))
```

### Key Specs
*   **Protocol:** DALI / DALI-2 (Forward & Backward frames)
*   **Control:** Dimming, Groups, Scenes, Broadcast
*   **DT8 Support:** Tunable White (Tc) & RGB (RGBWAF)
*   **Input Devices:** Event monitoring for buttons and sensors (Instance types).
