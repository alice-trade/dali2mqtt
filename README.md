# DALI-to-MQTT Bridge for ESP

![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.x-blue)
![Language](https://img.shields.io/badge/language-C++20-purple)

**dali2mqtt** is a bridge between the DALI lighting control bus and the MQTT protocol, designed for the ESP32 platform using the ESP-IDF framework. This project allows you to integrate professional DALI lighting systems into modern smart home ecosystems such as Home Assistant, Node-RED, and others.

## Architecture

The **dali2mqtt** firmware runs on an ESP32-S3/C6/etc. acting as a bridge between a standard DALI Bus (requires a physical DALI Driver circuit) and your MQTT Broker.

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

## Key Capabilities

*   **DALI Control**: Send brightness commands (DACP), ON/OFF, DT8 commands and Scene recall commands to individual devices (short address) and groups (group address).
*   **Two-Way Communication**: Poll luminaire status (brightness level, lamp status) and publish this data to MQTT.
*   **Passive Monitoring**: The bridge constantly "listens" to the DALI bus. If a command is sent from another device (e.g., a wall-mounted DALI controller), the bridge detects this change and sends the updated state to MQTT, ensuring full synchronization.
*   **DALI Bus Management**:
    *   **Automatic Addressing (Commissioning)**: Launch the initialization process to discover new devices on the bus and automatically assign them short addresses.
    *   **Bus Scanning**: Ability to scan for all active, already addressed devices at any time.
*   **DALI Group Management**: Visually assign devices to any of the 16 DALI groups via the Web Interface. Group control is also available via MQTT.
*   **Web UI**: Built-in web server for easy WiFi/MQTT setup, as well as DALI bus management (scanning, initialization, groups, scenes).
*   **Home Assistant Auto-Discovery**: Option which automatically publishes configuration messages to integrate DALI lights and groups into Home Assistant without manual configuration.

## Hardware Requirements

1.  **ESP-based Board**: Any board with an ESP32-S3 or ESP32-C6 module (e.g., ESP32-S3-DevKitC) with Wifi module.
2.  **DALI Transceiver/Power Supply**: A specialized circuit/device that provides DALI bus power and converts ESP32 logic levels to DALI electrical signals.

## Software Requirements

1.  **ESP-IDF v5.x**: [Installation Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html).
2.  **GCC** (xtensa/riscv-esp-elf-g++): The C/C++ compiler provided by ESP-IDF.
3.  **Git**: To clone the repository.
4.  **Node.js and npm**: To build the Web UI frontend.

## Building and Flashing

### 1. Clone the Repository

```bash
git clone <YOUR_REPO_URL>
cd daliMQTT
```

### 2. Project Configuration

All main settings are exposed in `menuconfig`. You can change DALI pins, default parameters, and other options.

```bash
# Activate ESP-IDF environment
. $HOME/esp/esp-idf/export.sh

# Run menuconfig
cmake -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/esp-idf/tools/cmake/toolchain-esp<chip>.cmake -GNinja .
cmake --build build --target menuconfig
```
Main settings are located under the `DALI MQTT Bridge Settings` section.

### 3. Build and Flash

The process consists of three steps: building, flashing the main firmware, and flashing the file system for the Web UI.

```bash
# Build the project
cmake --build build

# Flash the main application
cmake --build build --target flash

# Start the serial monitor to view logs
cmake --build build --target monitor
```

## Initial Setup (Provisioning)

On the first boot, the device does not have credentials to connect to your network. It will enter provisioning mode:

1.  **Connect to Wi-Fi**: The device will create an Access Point with SSID `DALI-MQTT-Bridge` and password `bridge123` (can be changed in `menuconfig`).
2.  **Open Web UI**: Navigate to `http://dalimqtt.local` in your browser.
3.  **Enter Credentials**: Default is `admin` / `dalimqttbrg12321`.
4.  **Fill in the fields**:
    *   **WiFi Settings**: SSID and password for your home Wi-Fi network.
    *   **MQTT Settings**: Your broker URI (e.g., `mqtt://user:pass@192.168.1.100`), Client ID, and Base Topic.
    *   **Web UI Authentication**: New username and password for Web UI access.
5.  **Save and Restart**: Click "Save and Restart". The device will save the configuration to NVS memory and reboot, attempting to connect to your network.

## DALI Bus Setup

Once the device connects to your network, you can configure devices on the DALI bus:

1.  **Open Web UI**: Navigate to the device's IP address or `http://dalimqtt.local` (if mDNS is working on your network).
2.  **Go to the "DALI Control" tab**.
3.  **Select an Action**:
    *   **Scan Bus**: Use this if your DALI devices already have short addresses assigned. The system will scan all 64 addresses and display found devices.
    *   **Initialize New Devices**: Use this for new, unaddressed devices. The system will run the standard DALI commissioning procedure, find all devices without addresses, and sequentially assign them free short addresses (0 to 63).
    *   **Configure Groups**: Switch to the "Group Assignments" tab to assign each device to one or more of the 16 DALI groups.
    *   **Configure Scenes**: Switch to the "Scene Editor" tab to set brightness levels for each device in each of the 16 scenes. Settings are stored directly in the DALI ballasts' memory.
4.  Found devices will be automatically saved and the bridge will begin polling them to send states to MQTT.

## MQTT API

After successful configuration, the bridge will start interacting with the MQTT broker.

### Light Control (Command Topic)

The topic structure for sending commands is:
`{base_topic}/light/{type}/{id}/set`

*   `{base_topic}`: Base topic configured in settings (default `dali_bridge`).
*   `{type}`: `short` or `group`.
*   `{id}`: Device address (0-63) or Group ID (0-15).

**Payload Example (JSON):**

```json
{
  "state": "ON",
  "brightness": 254
}
```

*   `state`: `"ON"` or `"OFF"`.
*   `brightness`: `0`-`254`.

### Get State (State Topic)

The bridge periodically polls devices and publishes their state to:
`{base_topic}/light/short/{id}/state`

**Payload Example (JSON):**

```json
{
  "state": "ON",
  "brightness": 128
}
```

### Scene Control (Scene Command Topic)

Activate DALI scenes by sending a command to:
`{base_topic}/scene/set`

**Payload (Home Assistant):**

Home Assistant sends a simple string value corresponding to the selected option:

```
Scene 5
```

**Payload (JSON):**

```json
{
  "scene": 5
}
```

*   `scene`: Scene number from `0` to `15`.

### Group Management (Group Command Topic)

Add or remove a device from a group:
`{base_topic}/config/group/set`

**Payload Example (JSON):**

```json
{
  "short_address": 10,
  "group": 3,
  "state": "add"
}
```
*   `short_address`: Device address (0-63).
*   `group`: Group number (0-15).
*   `state`: `"add"` or `"remove"`.

The operation result is published to `{base_topic}/config/group/result`.

### Availability Status (Availability Topic)

The bridge reports its status in the topic:
`{base_topic}/status`

*   Payload `online`: Bridge is connected to MQTT.
*   Payload `offline`: LWT (Last Will and Testament) message, sent by the broker if the bridge disconnects.

## Development and Testing

The project contains a set of unit and integration tests to ensure code quality.

### Building Tests

To build the test firmware, pass the `BUILD_TESTS=ON` flag to the CMake command:

```bash
# Configuration for building tests
cmake -B build -G Ninja -DBUILD_TESTS=ON

# Build and run tests
cmake --build build --target test
cmake --build build --target test-flash
cmake --build build --target test-monitor
```

## Project Structure

```
.
├── support/              # ESP-IDF configuration for ESP chip targets
├── scripts/              # Helper CMake scripts
├── examples/             # Examples
├── Kconfig/              # Project compile-time configuration
├── src/DaliMQX           # JS Library source code
├── src/DaliMQTT          # Firmware source code
│   ├── config/           # Configuration Manager
│   ├── dali/             # High-level DALI API
│   │   └── driver/       # Low-level DALI driver (gptimer, GPIO)
│   ├── lifecycle/        # Application lifecycle and mode coordination
│   ├── mqtt/             # MQTT client and Auto-Discovery logic
│   ├── webui/            # Web UI source code (Vue.js + C++)
│   ├── wifi/             # Wi-Fi connection manager
│   └── main.cxx          # Application entry point
└── tests/                # Source code for tests (incomplete)
```