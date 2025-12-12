import { EventEmitter } from 'events';
import { IMqttDriver } from './interfaces/MqttDriver';
import { DaliDevice } from './entities/Device';
import { DaliGroup } from './entities/Group';
import { DaliInputDevice } from './entities/Input';
import { DaliBridgeOptions } from './interfaces/Common';

interface RawCommandRequest {
    tag: string;
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

    private pendingCommands = new Map<string, RawCommandRequest>();

    constructor(
        private readonly mqtt: IMqttDriver,
        options: DaliBridgeOptions = {}
    ) {
        super();
        this.baseTopic = options.baseTopic || 'dali_mqtt';
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
            `${this.baseTopic}/light/+/attributes`,
            `${this.baseTopic}/light/+/groups`,
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
    public getAllDevices(): DaliDevice[] {
        return Array.from(this.devices.values());
    }

    public getGroup(groupId: number): DaliGroup {
        if (!this.groups.has(groupId)) {
            this.groups.set(groupId, new DaliGroup(this.mqtt, this.baseTopic, groupId, this));
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
    public async sendCommand(addr: number, cmd: number, bits: 16 | 24 = 16, twice: boolean = false): Promise<any> {
        return new Promise((resolve, reject) => {
            const tag = Math.random().toString(36).substring(7);
            const payload = { addr, cmd, bits, twice, tag };

            this.pendingCommands.set(tag, {
                tag,
                resolve,
                reject,
                timestamp: Date.now()
            });

            setTimeout(() => {
                if (this.pendingCommands.has(tag)) {
                    this.pendingCommands.get(tag)?.reject(new Error('DALI Command Timeout'));
                    this.pendingCommands.delete(tag);
                }
            }, 3000);

            this.mqtt.publish(`${this.baseTopic}/cmd/send`, JSON.stringify(payload));
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

            if (topic.includes('/light/')) {
                const parts = topic.split('/');
                const suffix = parts[parts.length - 1];

                if (parts[parts.length - 3] === 'group') {
                    const groupId = parseInt(parts[parts.length - 2]);
                    if (suffix === 'state') {
                        this.getGroup(groupId).updateState(this.mapState(data));
                    }
                } else {
                    const addr = parts[parts.length - 2];
                    const device = this.getDevice(addr);

                    if (suffix === 'state') {
                        device.updateState(this.mapState(data));
                    } else if (suffix === 'status') {
                        const isOnline = strPayload === 'online' || strPayload === 'ON' || strPayload === 'TRUE' || data?.state === 'ONLINE';
                        device.updateState({ available: isOnline });
                    } else if (suffix === 'attributes') {
                        device.updateState({
                            gtin: data.gtin,
                            device_type: data.device_type,
                            min_level: data.dev_min_level,
                            max_level: data.dev_max_level
                        });
                    } else if (suffix === 'groups') {
                        if (Array.isArray(data.groups)) {
                            device.updateState({ groups: data.groups });
                        }
                    }
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
            // catch
        }
    }

    private handleCommandResponse(data: any) {
        if (data && data.tag && this.pendingCommands.has(data.tag)) {
            const req = this.pendingCommands.get(data.tag)!;
            this.pendingCommands.delete(data.tag);

            if (data.status === 'ok') {
                req.resolve(data);
            } else {
                // TODO: reject?
                req.resolve(data);
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