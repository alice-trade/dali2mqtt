# Configuration Guide

Configuration is stored in NVS (Non-Volatile Storage) on the ESP32. You can modify these settings via the WebUI or via MQTT (JSON).

## Network Settings

| Setting | Description |
| :--- | :--- |
| `wifi_ssid` | SSID of your WiFi network (2.4GHz). |
| `wifi_pass` | Password for WiFi. |
| `http_domain` | mDNS domain. Default `dali-bridge` -> `http://dali-bridge.local`. |

## MQTT Settings

| Setting | Description |
| :--- | :--- |
| `mqtt_uri` | Full URI, e.g., `mqtt://192.168.1.5:1883`. |
| `mqtt_user` | MQTT Username (optional). |
| `mqtt_pass` | MQTT Password (optional). |
| `client_id` | MQTT Client ID. Also used for Home Assistant discovery ID. |
| `mqtt_base` | Root topic prefix. Default: `dali_mqtt`. |

## DALI Engine

| Setting | Description |
| :--- | :--- |
| `dali_poll` | Interval (in ms) for background syncing of device states. Default `200000` (200s). |

## Updating via MQTT

You can push a full configuration JSON to `{base}/config/set`. The device will update NVS and reboot if network settings changed.

**Payload Example:**
```json
{
  "dali_poll": 60000,
  "syslog_enabled": true,
  "syslog_srv": "192.168.1.50"
}
```
