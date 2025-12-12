import { IMqttDriver } from '../interfaces/MqttDriver';
import { DaliState } from '../interfaces/Common';
import { ColorUtils } from '../utils/Color';
import { EventEmitter } from 'events';
import {DaliBridge} from "../Bridge";
import {DaliDevice} from "./Device";

/**
 * Represents a DALI Group (0-15).
 * Allows broadcasting commands to multiple devices simultaneously.
 */
export class DaliGroup extends EventEmitter {
    private _state: DaliState = {
        state: 'OFF',
        brightness: 0
    };

    constructor(
        private readonly mqtt: IMqttDriver,
        private readonly baseTopic: string,
        public readonly groupId: number,
        private readonly bridge: DaliBridge

    ) {
        super();
        if (groupId < 0 || groupId > 15) {
            throw new Error(`Invalid DALI Group ID: ${groupId}. Must be 0-15.`);
        }
    }

    /**
     * Gets the last known state of the group.
     */
    public get state(): Readonly<DaliState> {
        return this._state;
    }

    private get commandTopic(): string {
        return `${this.baseTopic}/light/group/${this.groupId}/set`;
    }

    public getDevices(): DaliDevice[] {
        const allDevices = this.bridge.getAllDevices();
        return allDevices.filter(dev => {
            const groups = dev.getGroups();
            return groups.includes(this.groupId);
        });
    }
    /**
     * Turns the group ON.
     */
    public async turnOn(): Promise<void> {
        await this.sendJson({ state: 'ON' });
    }

    /**
     * Turns the group OFF.
     */
    public async turnOff(): Promise<void> {
        await this.sendJson({ state: 'OFF' });
    }

    /**
     * Sets the brightness for the group.
     * @param level Brightness level (0-254).
     */
    public async setBrightness(level: number): Promise<void> {
        const val = Math.max(0, Math.min(254, Math.round(level)));
        await this.sendJson({ state: 'ON', brightness: val });
    }

    /**
     * Sets Color Temperature for the group.
     * @param kelvin Temperature in Kelvin.
     */
    public async setCCT(kelvin: number): Promise<void> {
        const mireds = ColorUtils.kelvinToMireds(kelvin);
        await this.sendJson({ state: 'ON', color_temp: mireds });
    }

    /**
     * Sets RGB Color for the group.
     */
    public async setRGB(r: number, g: number, b: number): Promise<void> {
        await this.sendJson({ state: 'ON', color: { r, g, b } });
    }

    private async sendJson(payload: object): Promise<void> {
        await this.mqtt.publish(this.commandTopic, JSON.stringify(payload));
    }

    /** @internal */
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