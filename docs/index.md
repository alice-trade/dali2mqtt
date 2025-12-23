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
      link: https://github.com/youruser/DaliMQTT

features:
  - title: Fully Featured DALI Engine
    details: Supports addressing, groups, scenes, and DT8 (Color Temp & RGB) control straight out of the box.
    icon: 💡
  - title: Home Assistant Discovery
    details: Automatically appears in Home Assistant. Bidirectional sync with low latency.
    icon: 🔌
  - title: WebUI
    details: Built-in Vue.js interface for commissioning, addressing, and configuration stored on SPIFFS.
    icon: 🖥
  - title: ESP32 Hardware Support
    details: Runs on ESP32-* for reliable DALI communication.
    icon: ⚡️
---

## Overview
### Architecture
*Schema: ESP32 <-> DALI Bus Circuit <-> Drivers*

### Quick Specs
*   **Protocol:** DALI / DALI-2 (Backward frames support)
*   **Color Control:** DT8 Tc (Tunable White) & RGB support
*   **Connectivity:** WiFi (STA + AP Mode for provisioning)
*   **Update:** OTA Support via WebUI
