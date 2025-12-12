import { EventEmitter } from 'events';

export interface InputEvent {
    event_code: number;
    instance: number;
    type: 'short' | 'long' | 'double' | 'motion' | 'unknown';
    Data: any;
}

/**
 * Represents a DALI Input Device (e.g., Push Button, Motion Sensor).
 * Listens for events from the DALI bus.
 */
export class DaliInputDevice extends EventEmitter {
    constructor(
        public readonly longAddress: string
    ) {
        super();
    }

    /**
     * Internal method to process incoming MQTT event data.
     * @internal
     */
    public processEvent(data: any): void {
        const eventCode = data.event_code;
        const instance = data.instance;

        let type: InputEvent['type'] = 'unknown';

        const event: InputEvent = {
            event_code: eventCode,
            instance: instance,
            type: type,
            Data: data
        };

        this.emit('event', event);

        // Alias events for easier usage
        // TODO: Real implementations should check the specific Event Scheme of the device
        this.emit('raw_event', eventCode, instance);
    }
}