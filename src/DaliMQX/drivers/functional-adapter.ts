import { IMqttDriver } from '../interfaces/mqtt-driver';

type PublishFn = (topic: string, payload: string | Buffer, options?: any) => Promise<void> | void;
type SubscribeFn = (topic: string) => Promise<void> | void;


export class FunctionalDriver implements IMqttDriver {
    private messageHandler: ((topic: string, payload: Buffer) => void) | null = null;

    constructor(
        private publishFn: PublishFn,
        private subscribeFn: SubscribeFn
    ) {}

    async publish(topic: string, payload: string | Buffer, options?: any): Promise<void> {
        await this.publishFn(topic, payload, options);
    }

    async subscribe(topic: string): Promise<void> {
        await this.subscribeFn(topic);
    }

    onMessage(callback: (topic: string, payload: Buffer) => void): void {
        this.messageHandler = callback;
    }

    /**
     * Call this method when a message is received from the external system (e.g., Node-RED input).
     */
    public simulateIncomingMessage(topic: string, payload: Buffer | string): void {
        if (this.messageHandler) {
            const buffer = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
            this.messageHandler(topic, buffer);
        }
    }
}