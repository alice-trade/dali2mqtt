import { EventEmitter } from 'events';
import { IMqttDriver } from './interfaces/mqtt-driver';
import { DaliDevice } from './entities/device';
import { DaliGroup } from './entities/group';
import { DaliInputDevice } from './entities/input';
import { DaliBridgeOptions } from './types';

interface RawCommandRequest {
    addr: number;
    cmd: number;
    resolve: (val: any) => void;
    reject: (err: Error) => void;
    timestamp: number;
}

/**
 * Main class to interact with the DALI-MQTT Bridge.
 */
export class DaliBridge extends EventEmitter {
    private readonly baseTopic: string;
    private devices = new Map<string, DaliDevice>();
    private groups = new Map<number, DaliGroup>();
    private inputDevices = new Map<string, DaliInputDevice>();

    // Queue for Raw Command responses
    private pendingCommands: RawCommandRequest[] = [];

    constructor(
        private readonly mqtt: IMqttDriver,
        options: DaliBridgeOptions = {}
    ) {
        super();
        this.baseTopic = options.baseTopic || 'dali_mqtt';

        // Setup message routing
        this.mqtt.onMessage(this.handleMessage.bind(this));
    }

    /**
     * Initializes the bridge connection.
     * Subscribes to necessary topics.
     */
    public async start(): Promise<void> {
        const topics = [
            `${this.baseTopic}/light/+/state`,
            `${this.baseTopic}/light/+/status`,
            `${this.baseTopic}/light/group/+/state`,
            `${this.baseTopic}/event/#`,
            `${this.baseTopic}/cmd/res`,
            `${this.baseTopic}/config/bus/sync_status`
        ];

        for (const topic of topics) {
            await this.mqtt.subscribe(topic);
        }
    }

    /**
     * Get a Device entity.
     * @param longAddress 24-bit Long Address (Hex).
     */
    public getDevice(longAddress: string): DaliDevice {
        const addr = longAddress.toUpperCase();
        if (!this.devices.has(addr)) {
            this.devices.set(addr, new DaliDevice(this.mqtt, this.baseTopic, addr));
        }
        return this.devices.get(addr)!;
    }

    /**
     * Get a Group entity.
     * @param groupId Group ID (0-15).
     */
    public getGroup(groupId: number): DaliGroup {
        if (!this.groups.has(groupId)) {
            this.groups.set(groupId, new DaliGroup(this.mqtt, this.baseTopic, groupId));
        }
        return this.groups.get(groupId)!;
    }

    /**
     * Get an Input Device entity.
     * @param longAddress 24-bit Long Address (Hex).
     */
    public getInputDevice(longAddress: string): DaliInputDevice {
        const addr = longAddress.toUpperCase();
        if (!this.inputDevices.has(addr)) {
            this.inputDevices.set(addr, new DaliInputDevice(addr));
        }
        return this.inputDevices.get(addr)!;
    }

    /**
     * Send a Raw DALI Command.
     * @param addr Address (Short, Group, or Special).
     * @param cmd Command Byte.
     * @param bits 16 or 24 bit frame.
     * @param twice Send twice (required for some config commands).
     * @returns Promise resolving to the response from the bus (if any).
     */
    public async sendRawCommand(addr: number, cmd: number, bits: 16 | 24 = 16, twice: boolean = false): Promise<any> {
        return new Promise((resolve, reject) => {
            const payload = { addr, cmd, bits, twice };

            this.pendingCommands.push({
                addr,
                cmd,
                resolve,
                reject,
                timestamp: Date.now()
            });

            setTimeout(() => {
                const idx = this.pendingCommands.findIndex(c => c.addr === addr && c.cmd === cmd);
                if (idx !== -1) {
                    this.pendingCommands[idx].reject(new Error('DALI Command Timeout'));
                    this.pendingCommands.splice(idx, 1);
                }
            }, 3000);

            this.mqtt.publish(`${this.baseTopic}/cmd/raw`, JSON.stringify(payload));
        });
    }

    /**
     * Scan the bus for devices.
     */
    public async scanBus(): Promise<void> {
        await this.mqtt.publish(`${this.baseTopic}/config/bus/scan`, '{}');
    }

    /**
     * Assign a device to a group.
     * @param longAddress Device Long Address.
     * @param groupId Group ID (0-15).
     * @param action 'add' or 'remove'.
     */
    public async assignGroup(longAddress: string, groupId: number, action: 'add' | 'remove'): Promise<void> {
        const topic = `${this.baseTopic}/config/group/set`;
        const payload = {
            long_address: longAddress,
            group: groupId,
            state: action
        };
        await this.mqtt.publish(topic, JSON.stringify(payload));
    }

    private handleMessage(topic: string, payload: Buffer): void {
        const strPayload = payload.toString();

        try {
            const data = JSON.parse(strPayload);

            if (topic.endsWith('/cmd/res')) {
                this.handleCommandResponse(data);
                return;
            }

            if (topic.includes('/light/') && topic.endsWith('/state')) {
                const parts = topic.split('/');
                if (parts[parts.length - 3] === 'group') {
                    // Group State
                    const groupId = parseInt(parts[parts.length - 2]);
                    this.getGroup(groupId).updateState(this.mapState(data));
                } else {
                    // Device State
                    const addr = parts[parts.length - 2];
                    this.getDevice(addr).updateState(this.mapState(data));
                }
            }

            else if (topic.endsWith('/status') && !topic.includes('sync_status')) {
                const parts = topic.split('/');
                if (parts[parts.length - 3] !== 'group') {
                    const addr = parts[parts.length - 2];
                    const isOnline = strPayload === 'online' || strPayload === 'ON' || strPayload === 'TRUE';
                    this.getDevice(addr).updateState({ available: isOnline });
                }
            }

            else if (topic.includes('/event/')) {
                const parts = topic.split('/');
                const type = parts[parts.length - 2];
                const addr = parts[parts.length - 1];

                this.emit('event', { type, addr, data });

                if (type === 'long') {
                    this.getInputDevice(addr).processEvent(data);
                }
            }

        } catch (err) {
        }
    }

    private handleCommandResponse(data: any) {
        const idx = this.pendingCommands.findIndex(c => c.addr === data.addr && c.cmd === data.cmd);

        if (idx !== -1) {
            const cmd = this.pendingCommands[idx];
            this.pendingCommands.splice(idx, 1);

            if (data.status === 'ok') {
                cmd.resolve(data);
            } else {
                cmd.resolve(data);
            }
        }
    }

    private mapState(data: any) {
        return {
            state: data.state,
            brightness: data.brightness,
            color_temp: data.color_temp,
            color: data.color,
            status_byte: data.status_byte
        };
    }
}