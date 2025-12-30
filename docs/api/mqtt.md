# MQTT API Reference

The bridge uses a structured topic hierarchy.
**Base Topic:** Defaults to `dali_mqtt`, configurable in settings.

::: tip
`{long_addr}` refers to the 24-bit Hex address of the device (e.g., `AABBCC`).
`{group_id}` refers to DALI Group ID (0-15).
:::

## Lighting Control

### Control Single Device
**Topic:** `{base}/light/{long_addr}/set`
**Payload:** JSON
```json
{
  "state": "ON",          
  "brightness": 254,      
  "color_temp": 300,      
  "color": {             
    "r": 255,
    "g": 0,
    "b": 0
  }
}
```

### Control Group
**Topic:** `{base}/light/group/{group_id}/set`
**Payload:** Same as Single Device.

### Broadcast
**Topic:** `{base}/light/broadcast/set`
**Payload:** Same as Single Device. Commands sent to `0xFE` (Broadcast).

---

## State & Telemetry

### Device State
**Topic:** `{base}/light/{long_addr}/state`
**Retain:** True
**Payload:**
```json
{
  "state": "ON",
  "brightness": 200,
  "status_byte": 4,   
  "color_temp": 300, 
  "color": {"r":"..."}
}
```

### Availability
**Topic:** `{base}/light/{long_addr}/status`
**Payload:** `online` or `offline`

### Attributes (Static Info)
**Topic:** `{base}/light/{long_addr}/attributes`
**Payload:**
```json
{
  "device_type": 6,     
  "gtin": "123456...",
  "dev_min_level": 1,
  "dev_max_level": 254,
  "short_address": 5
}
```

---

## Input Devices & Events

Events from switches, motion sensors, etc.

**Topic:** `{base}/event/{type}/{address}`
*   `type`: `short`, `group`, `long` (if mapped).
*   `address`: Short Address or Long Address.

**Payload:**
```json
{
  "type": "event",
  "address_type": "short",
  "address": 5,
  "instance": 0,   
  "event_code": 1   
}
```

---

## Management API

### Sync Request
Force the bridge to poll devices.
**Topic:** `{base}/cmd/sync`
**Payload:**
```json
{
  "addr_type": "short",
  "address": "AABBCC",
  "delay_ms": 0
}
```

### Raw DALI Command
Send raw frames to the bus.
**Topic:** `{base}/cmd/send`
**Payload:**
```json
{
  "addr": 5,
  "cmd": 144,
  "bits": 16,
  "tag": "req_1"
}
```
**Response Topic:** `{base}/cmd/res` (if `tag` provided).

### Bus Scan
Trigger a full bus scan.
**Topic:** `{base}/config/bus/scan`
**Payload:** `{}`

### Assign Group
**Topic:** `{base}/config/group/set`
**Payload:**
```json
{
  "long_address": "AABBCC",
  "group": 0,
  "state": "add"
}
```
