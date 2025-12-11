/**
 * Interface representing the required MQTT functionality.
 * This allows dependency injection of different MQTT libraries (mqtt.js, paho, internal Node-RED routing).
 */
export interface IMqttDriver {
    /**
     * Publishes a message to a topic.
     * @param topic The MQTT topic.
     * @param payload The message payload (string or buffer).
     * @param options Optional MQTT parameters (qos, retain).
     */
    publish(topic: string, payload: string | Buffer, options?: { qos?: 0 | 1 | 2; retain?: boolean }): Promise<void>;

    /**
     * Subscribes to a topic pattern.
     * @param topic The topic or wildcard pattern.
     */
    subscribe(topic: string): Promise<void>;

    /**
     * Register a callback for incoming messages.
     * @param callback Function to handle incoming messages.
     */
    onMessage(callback: (topic: string, payload: Buffer) => void): void;
}