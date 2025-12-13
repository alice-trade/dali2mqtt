import { Injectable, OnModuleInit, OnModuleDestroy, Logger } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import * as mqtt from 'mqtt';
import { DaliBridge, MqttJsAdapter, DaliDevice } from 'dalimqx';

@Injectable()
export class DaliService implements OnModuleInit, OnModuleDestroy {
    private bridge: DaliBridge;
    private mqttClient: mqtt.MqttClient;
    private readonly logger = new Logger(DaliService.name);

    constructor(private configService: ConfigService) {}

    async onModuleInit() {
        const mqttUri = this.configService.get<string>('MQTT_URI', 'mqtt://localhost:1883');
        const baseTopic = this.configService.get<string>('DALI_BASE_TOPIC', 'dali_mqtt');
        const username = this.configService.get<string>('MQTT_USERNAME');
        const password = this.configService.get<string>('MQTT_PASSWORD');

        this.logger.log(`Connecting to MQTT broker at ${mqttUri}...`);

        const mqttOptions: mqtt.IClientOptions = {
            username: username,
            password: password,
        };

        this.mqttClient = mqtt.connect(mqttUri, mqttOptions);

        const adapter = new MqttJsAdapter(this.mqttClient);

        this.bridge = new DaliBridge(adapter, { baseTopic });

        this.mqttClient.on('connect', async () => {
            this.logger.log('MQTT Connected. Starting DaliBridge...');
            await this.bridge.start();

        });

        this.mqttClient.on('error', (err) => {
            this.logger.error('MQTT Error', err);
        });

        this.bridge.on('event', (event) => {
            this.logger.log(`DALI Event received: ${JSON.stringify(event)}`);
        });
    }

    onModuleDestroy() {
        if (this.mqttClient) {
            this.mqttClient.end();
        }
    }

    getAllDevices() {
        const devices = this.bridge.getAllDevices();
        return devices.map(d => ({
            longAddress: d.longAddress,
            state: d.state
        }));
    }

    async getDeviceState(addr: string) {
        const device = this.bridge.getDevice(addr);
        return device.state;
    }

    async turnOn(addr: string) {
        const device = this.bridge.getDevice(addr);
        await device.turnOn();
        return { status: 'ok', action: 'turnOn', addr };
    }

    async turnOff(addr: string) {
        const device = this.bridge.getDevice(addr);
        await device.turnOff();
        return { status: 'ok', action: 'turnOff', addr };
    }

    async setBrightness(addr: string, level: number) {
        const device = this.bridge.getDevice(addr);
        await device.setBrightness(level);
        return { status: 'ok', action: 'setBrightness', level, addr };
    }
}