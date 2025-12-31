# HTTP API Reference

The Bridge provides a REST API used by the WebUI.
**Authentication:** Basic Auth required.

## Configuration

### Get Configuration
*   **GET** `/api/config`
*   **Response:** JSON with current settings (passwords masked).

### Set Configuration
*   **POST** `/api/config`
*   **Payload:** JSON (see `AppConfig` structure).
*   **Note:** Changing WiFi/MQTT settings triggers a reboot.

## DALI Operations

### Get Devices
*   **GET** `/api/dali/devices`
*   **Response:**
    ```json
    [
      {
        "long_address": "AABBCC",
        "short_address": 0,
        "level": 254,
        "available": true,
        "dt": 6
      }
    ]
    ```

### Scan Bus
*   **POST** `/api/dali/scan`
*   **Response:** `202 Accepted`. Starts background task.

### Initialize Bus
*   **POST** `/api/dali/initialize`
*   **Response:** `202 Accepted`. Starts addressing sequence.

### Group Management
*   **GET** `/api/dali/groups`
    *   Returns map of Long Address -> Array of Group IDs.
*   **POST** `/api/dali/groups`
    *   **Payload:** `{ "AABBCC": [0, 1], "DDEEFF": [2] }`
    *   Applies configuration to ballasts.

### Scene Management
*   **GET** `/api/dali/scenes?id=0`
    *   Returns levels for Scene 0.
*   **POST** `/api/dali/scenes`
    *   **Payload:**
        ```json
        {
          "scene_id": 0,
          "levels": {
            "AABBCC": 254,
            "DDEEFF": 128
          }
        }
        ```

## OTA Update

*   **POST** `/api/ota/pull`
*   **Payload:** `{"url": "http://server/firmware.bin"}`
*   Triggers ESP-IDF HTTPS OTA update.
