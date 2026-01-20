# Getting Started

## Hardware Requirements

1.  **ESP32 Board**: ESP32, ESP32-S2, S3, C3, etc.
2.  **DALI Physical Interface**: The ESP32 operates at 3.3V. The DALI bus operates at ~16V. **You need a hardware interface (circuit)** containing optocouplers/transistors to convert the signal.
    *   *RX Pin*: Configurable (build-time).
    *   *TX Pin*: Configurable (build-time).

## Flashing & First Boot

1.  Get and unpack firmware [last release](https://github.com/alice-trade/dali2mqtt/releases) to your ESP32;
2.  Run the flash.sh script to flash the firmware to your ESP;
3.  On the first boot, the device will be in **Provisioning Mode**.

### Provisioning Mode

If the device is not configured, it starts a WiFi Access Point:

*   **SSID:** `DALI-MQTT-Bridge`
*   **Password:** `bridge123` (or as configured in `CONFIG_DALI2MQTT_WIFI_AP_PASS`)
*   **WebUI:** Device DHCP IP or http://dalimqtt.local mdns domain

Login with default credentials:
*   **User:** `admin`
*   **Pass:** `dalimqttbrg12321`

Go to **Settings** and configure:
1.  **WiFi SSID & Password**: To connect to your home network.
2.  **MQTT Broker**: URI (e.g., `mqtt://192.168.1.10:1883`), User, Password.
3.  **Client ID**: Unique name for this bridge.
4.  **Save & Reboot**.

## Normal Operation

After reboot, the device connects to your WiFi.
*   **WebUI:** Accessible at `http://<device-ip>/` or via mDNS `http://<http_domain>.local`.
*   **Status LED:** (If configured) indicates MQTT connection status.

### Factory Reset
Hold the **BOOT Button (GPIO 0)** for **5 seconds** to reset the configuration and return to Provisioning Mode.
