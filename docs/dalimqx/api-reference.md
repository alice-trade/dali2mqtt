# API Reference

## DaliBridge

The main entry point for the library. It manages the MQTT connection, tracks device states, and handles incoming events.

#### Constructor

```typescript
new DaliBridge(driver: IMqttDriver, options?: DaliBridgeOptions)
```

*   **driver**: An implementation of `IMqttDriver` (e.g., `MqttJsAdapter`).
*   **options**: Configuration object.
    *   `baseTopic` (string, optional): The MQTT base topic configured on the ESP32. Default: `'dali_mqtt'`.

#### Methods

*   **`start(): Promise<void>`**
    Initializes the bridge and subscribes to all necessary MQTT topics (states, events, responses). Must be called after the MQTT client connects.

*   **`getDevice(longAddress: string): DaliDevice`**
    Returns a `DaliDevice` instance for the given 24-bit Long Address.
    *   `longAddress`: Hex string (e.g., `'AABBCC'`).

*   **`getGroup(groupId: number): DaliGroup`**
    Returns a `DaliGroup` instance.
    *   `groupId`: Number `0`-`15`.

*   **`getInputDevice(longAddress: string): DaliInputDevice`**
    Returns a `DaliInputDevice` instance for handling events from sensors/switches.
    *   `longAddress`: Hex string.

*   **`getAllDevices(): DaliDevice[]`**
    Returns an array of all devices currently known/tracked by the bridge.

*   **`scanBus(): Promise<void>`**
    Triggers a background bus scan on the ESP32. Updates are received asynchronously.

*   **`assignGroup(longAddress: string, groupId: number, action: 'add' | 'remove'): Promise<void>`**
    Adds or removes a device from a DALI group.
    *   `longAddress`: Device Hex address.
    *   `groupId`: Group ID `0`-`15`.
    *   `action`: `'add'` or `'remove'`.

*   **`sendCommand(addr: number, cmd: number, bits?: 16 | 24, twice?: boolean): Promise<any>`**
    Sends a raw DALI command to the bus.
    *   `addr`: Target address byte (Short, Group, or Special).
    *   `cmd`: Command byte.
    *   `bits`: Frame length (`16` default, or `24`).
    *   `twice`: Send command twice (required for some configuration commands).
    *   **Returns**: A Promise that resolves with the bus response (if any) or rejects on timeout.

#### Events

*   **`event`**
    Emitted when any DALI event (input device) occurs.
    *   **Payload**: `{ type: string, addr: string, data: any }`

---

## DaliDevice

Represents a controllable DALI luminaire (Short Address).

#### Properties

*   **`longAddress`** (string): The device's 24-bit address.
*   **`state`** (Readonly<`DaliState`>): The current cached state.

#### Status Getters (Boolean)

Helper properties to check specific bits of the DALI Status Byte:

*   `isLampFailure`: True if lamp failure is detected.
*   `isControlGearFailure`: True if driver failure is detected.
*   `isLampOn`: True if the lamp is currently ON.
*   `isLimitError`: True if requested level was out of bounds.
*   `isFadeRunning`: True if a fade is currently in progress.
*   `isResetState`: True if device is in reset state.
*   `isMissingShortAddress`: True if device has no short address.
*   `isPowerFailure`: True if device experienced a power cycle.

#### Methods

*   **`turnOn(): Promise<void>`**
    Turns the light ON (recalls max level or last level depending on device config).

*   **`turnOff(): Promise<void>`**
    Turns the light OFF.

*   **`setBrightness(level: number): Promise<void>`**
    Sets absolute brightness.
    *   `level`: `0` to `254`.

*   **`setCCT(kelvin: number): Promise<void>`**
    Sets Color Temperature (DT8 Tc).
    *   `kelvin`: Value in Kelvin (e.g., `2700` to `6500`).

*   **`setRGB(r: number, g: number, b: number): Promise<void>`**
    Sets RGB Color (DT8 RGBWAF).
    *   `r`, `g`, `b`: `0`-`255`.

*   **`getGroups(): number[]`**
    Returns an array of group IDs this device belongs to (based on cached state).

*   **`getGTIN(): string | undefined`**
    Returns the GTIN (Global Trade Item Number) if available.

#### Events

*   **`change`**
    Emitted when the device state changes (brightness, status, etc).
    *   **Payload**: `DaliState`

---

## DaliGroup

Represents a DALI Group (`0`-`15`). Commands sent to a group affect all member devices simultaneously.

#### Properties

*   **`groupId`** (number): The Group ID.
*   **`state`** (Readonly<`DaliState`>): The aggregated state of the group.

#### Methods

*   **`turnOn(): Promise<void>`**
*   **`turnOff(): Promise<void>`**
*   **`setBrightness(level: number): Promise<void>`**
*   **`setCCT(kelvin: number): Promise<void>`**
*   **`setRGB(r: number, g: number, b: number): Promise<void>`**
*   **`getDevices(): DaliDevice[]`**
    Returns a list of `DaliDevice` instances known to belong to this group.

#### Events

*   **`change`**
    Emitted when the group state updates.
    *   **Payload**: `DaliState`

---

## DaliInputDevice

Represents an Input Device (e.g., Button, Motion Sensor) identified by a Long Address.

#### Properties

*   **`longAddress`** (string): The device's 24-bit address.

#### Events

*   **`event`**
    Emitted when the device sends an event frame.
    *   **Payload** (`InputEvent`):
        ```typescript
        {
          event_code: number;
          instance: number;
          type: 'short' | 'long' | 'double' | 'motion' | 'unknown';
          Data: any;
        }
        ```

*   **`raw_event`**
    Emitted with raw arguments.
    *   **Args**: `(eventCode: number, instance: number)`

---

## Drivers / Adapters

## MqttJsAdapter

Standard adapter for the popular `mqtt` (MQTT.js) library.

```typescript
import mqtt from 'mqtt';
import { MqttJsAdapter } from 'dalimqx';

const client = mqtt.connect('mqtt://broker:1883');
const adapter = new MqttJsAdapter(client);
const bridge = new DaliBridge(adapter);
```

## FunctionalDriver

Adapter for custom environments (e.g., Node-RED, obscure MQTT libs) where you provide the publish/subscribe functions manually.

```typescript
import { FunctionalDriver } from 'dalimqx';

const adapter = new FunctionalDriver(
    (topic, payload) => { /* your publish logic */ },
    (topic) => { /* your subscribe logic */ }
);

// Inject incoming messages
adapter.simulateIncomingMessage('dali_mqtt/...', buffer);
```

---

## Types

## DaliState

Interface representing the state of a device or group.

```typescript
interface DaliState {
    state?: 'ON' | 'OFF';
    brightness?: number; // 0-254
    color_temp?: number; // Mireds
    color?: {
        r: number;
        g: number;
        b: number;
    };
    status_byte?: number;
    available?: boolean;
    device_type?: number;
    gtin?: string;
    groups?: number[];
    min_level?: number;
    max_level?: number;
}
```

## ColorUtils

Helper for color conversions.

*   **`static kelvinToMireds(kelvin: number): number`**
*   **`static miredsToKelvin(mireds: number): number`**
```