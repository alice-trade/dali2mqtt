# Home Assistant Integration

DaliMQTT is designed to work out-of-the-box with Home Assistant (HA) via **MQTT Discovery**.

## Prerequisites

1.  Mosquitto (or other MQTT Broker) running.
2.  Home Assistant with MQTT integration configured.
3.  DaliMQTT Bridge connected to the same broker.
4.  Option "Home Assistant Integration" enabled in device settings

## Automatic Discovery

Once the bridge connects to MQTT, it publishes discovery configurations to `homeassistant/light/...` and `homeassistant/select/...`.

### What appears in HA?

1.  **Lights:** Every addressed DALI device (Short Address 0-63) appears as a `Light` entity.
    *   **Features:** Brightness is always supported. Color Temp and RGB are enabled if the device reports DT8 capabilities.
2.  **Groups:** DALI Groups (0-15) appear as `Light` entities.
3.  **Scenes:** A `Select` entity allows you to trigger DALI Scenes (0-15).
4.  **Bridge Status:** A Device entity showing IP, Uptime, and Connectivity.

## Troubleshooting

If devices do not appear:
1.  Check that **MQTT Discovery** is enabled in Home Assistant.
2.  Check the broker logs.
3.  Restart the DaliMQTT bridge (it sends discovery payloads on `MQTT_EVENT_CONNECTED`).
