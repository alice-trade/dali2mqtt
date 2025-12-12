import { IMqttDriver } from '../interfaces/MqttDriver';
import { DaliState } from '../interfaces/Common';
import { EventEmitter } from 'events';
import {ColorUtils} from "../utils/Color";
// import {DaliBridge} from "../Bridge";

/**
 * Represents a single DALI device.
 */
export class DaliDevice extends EventEmitter {
    private _state: DaliState = {
        available: false,
        brightness: 0,
        state: 'OFF',
        groups: [],
        color: { r: 0, g: 0, b: 0 },
        color_temp: 0
    };

    constructor(
        private readonly mqtt: IMqttDriver,
        private readonly baseTopic: string,
        public readonly longAddress: string,
        // private readonly bridge: DaliBridge
    ) {
        super();
    }

    /**
     * Gets the current known state of the device.
     */
    public get state(): Readonly<DaliState> {
        return this._state;
    }

    /**
     * Topic used to send commands to this device.
     */
    private get commandTopic(): string {
        return `${this.baseTopic}/light/${this.longAddress}/set`;
    }

    /**
     * Turns the light ON.
     * Uses DALI RECALL_MAX_LEVEL if no specific brightness is cached, or restores previous level.
     */
    public async turnOn(): Promise<void> {
        await this.sendJson({ state: 'ON' });
    }

    /**
     * Turns the light OFF.
     */
    public async turnOff(): Promise<void> {
        await this.sendJson({ state: 'OFF' });
    }

    /**
     * Sets the absolute brightness.
     * @param level Brightness level (0-254).
     */
    public async setBrightness(level: number): Promise<void> {
        // Clamp value
        const val = Math.max(0, Math.min(254, Math.round(level)));
        await this.sendJson({ state: 'ON', brightness: val });
    }

    /**
     * Sets the Color Temperature (Tunable White).
     * @param kelvin Color temperature in Kelvin (e.g., 2700, 6500).
     */
    public async setCCT(kelvin: number): Promise<void> {
        const mireds = ColorUtils.kelvinToMireds(kelvin);
        await this.sendJson({ state: 'ON', color_temp: mireds });
    }

    /**
     * Sets the RGB Color.
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     */
    public async setRGB(r: number, g: number, b: number): Promise<void> {
        await this.sendJson({
            state: 'ON',
            color: { r, g, b }
        });
    }
    public getGroups(): number[] {
        return this._state.groups || [];
    }

    public getGTIN(): string | undefined {
        return this._state.gtin;
    }

    public get isControlGearFailure(): boolean {
        return this.checkStatusBit(0);
    }

    public get isLampFailure(): boolean {
        return this.checkStatusBit(1);
    }

    public get isLampOn(): boolean {
        return this.checkStatusBit(2);
    }

    public get isLimitError(): boolean {
        return this.checkStatusBit(3);
    }

    public get isFadeRunning(): boolean {
        return this.checkStatusBit(4);
    }

    public get isResetState(): boolean {
        return this.checkStatusBit(5);
    }

    public get isMissingShortAddress(): boolean {
        return this.checkStatusBit(6);
    }

    public get isPowerFailure(): boolean {
        return this.checkStatusBit(7);
    }

    private checkStatusBit(bit: number): boolean {
        const sb = this._state.status_byte;
        if (sb === undefined) return false;
        return ((sb >> bit) & 1) === 1;
    }
    /**
     * Internal helper to send JSON payload.
     */
    private async sendJson(payload: object): Promise<void> {
        await this.mqtt.publish(this.commandTopic, JSON.stringify(payload));
    }

    /**
     * Internal method called by the Bridge when a status update arrives.
     * @internal
     */
    public updateState(newState: Partial<DaliState>): void {
        const changed = Object.keys(newState).some(k =>
            (this._state as any)[k] !== (newState as any)[k]
        );

        this._state = { ...this._state, ...newState };

        if (changed) {
            this.emit('change', this._state);
        }
    }
}