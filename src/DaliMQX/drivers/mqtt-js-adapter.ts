import { IMqttDriver } from '../interfaces/mqtt-driver';
import { MqttClient } from 'mqtt';

/**
 * Adapter for the standard 'mqtt' (MQTT.js) library.
 */
export class MqttJsAdapter implements IMqttDriver {
    constructor(private client: MqttClient) {}

    publish(topic: string, payload: string | Buffer, options?: { qos?: 0 | 1 | 2; retain?: boolean }): Promise<void> {
        return new Promise((resolve, reject) => {
            this.client.publish(topic, payload, options || {}, (err) => {
                if (err) reject(err);
                else resolve();
            });
        });
    }

    subscribe(topic: string): Promise<void> {
        return new Promise((resolve, reject) => {
            this.client.subscribe(topic, (err) => {
                if (err) reject(err);
                else resolve();
            });
        });
    }

    onMessage(callback: (topic: string, payload: Buffer) => void): void {
        this.client.on('message', (topic, payload) => {
            callback(topic, payload);
        });
    }
}