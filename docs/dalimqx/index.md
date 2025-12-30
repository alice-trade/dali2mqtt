# DaliMQX Library

**DaliMQX** is a TypeScript/JavaScript library included with the project to easily interact with the DaliMQTT bridge. It wraps the MQTT topics into object-oriented entities.

## Installation

```bash
npm install dalimqx mqtt
```

## Basic Usage

### Connecting

```typescript
import mqtt from 'mqtt';
import { DaliBridge, MqttJsAdapter } from 'dalimqx';

// 1. Setup standard MQTT client
const client = mqtt.connect('mqtt://localhost:1883');

// 2. Wrap with Adapter
const adapter = new MqttJsAdapter(client);

// 3. Create Bridge instance
const bridge = new DaliBridge(adapter, { baseTopic: 'dali_mqtt' });

client.on('connect', async () => {
    console.log('Connected to MQTT');
    await bridge.start(); // Subscribes to topics
});
```

### Controlling Lights

```typescript
// Get device by Long Address (Hex)
const kitchenLight = bridge.getDevice('AABBCC');

// Simple ON/OFF
kitchenLight.turnOn();
kitchenLight.turnOff();

// Brightness (0-254)
kitchenLight.setBrightness(200);

// Tunable White (Kelvin)
kitchenLight.setCCT(4000);

// RGB Color
kitchenLight.setRGB(255, 0, 0);
```

### Controlling Groups

```typescript
const livingRoom = bridge.getGroup(0); // Group 0

livingRoom.turnOn();
livingRoom.setBrightness(128);
```

### Handling Events (Input Devices)

```typescript
// Listen for global events
bridge.on('event', (evt) => {
    console.log('DALI Event:', evt);
});

// Or specific input device
const switch1 = bridge.getInputDevice('FE0001'); // Long Address
switch1.on('event', (evt) => {
    if (evt.event_code === 1) {
        console.log('Short Press detected!');
    }
});
```

### Configuration via Code

```typescript
// Assign device AABBCC to Group 2
await bridge.assignGroup('AABBCC', 2, 'add');

// Scan the bus
await bridge.scanBus();
```
