import { ref, shallowReactive } from 'vue';
import mqtt from 'mqtt';
import { DaliBridge, MqttJsAdapter, DaliDevice } from 'dalimqx';

// Глобальное состояние
const isConnected = ref(false);
const isConnecting = ref(false);
const isScanning = ref(false);
const errorMsg = ref<string | null>(null);

const devices = shallowReactive(new Map<string, DaliDevice>());

let bridge: DaliBridge | null = null;
let client: mqtt.MqttClient | null = null;

export function useDali() {

    const connect = (host: string, port: number, baseTopic: string) => {
        if (isConnected.value || isConnecting.value) return;

        errorMsg.value = null;
        isConnecting.value = true;

        const url = `ws://${host}:${port}/mqtt`;
        console.log(`Connecting to ${url}...`);

        client = mqtt.connect(url, {
            clientId: 'dali_web_ui_' + Math.random().toString(16).substr(2, 8),
            keepalive: 60,
            reconnectPeriod: 2000,
            connectTimeout: 5000,
        });

        client.on('connect', async () => {
            console.log('MQTT Connected');
            isConnected.value = true;
            isConnecting.value = false;
            errorMsg.value = null;

            const adapter = new MqttJsAdapter(client!);
            bridge = new DaliBridge(adapter, { baseTopic });

            try {
                await bridge.start();
            } catch (e: any) {
                errorMsg.value = `Bridge Init Error: ${e.message}`;
            }

            startDevicePoller();
        });

        client.on('error', (err) => {
            console.error('MQTT Error', err);
            errorMsg.value = `Connection Error: ${err.message}`;
        });

        client.on('offline', () => {
            isConnected.value = false;
        });

        client.on('close', () => {
            isConnected.value = false;
            isConnecting.value = false;
        });
    };

    const disconnect = () => {
        if (client) {
            client.end();
            client = null;
            bridge = null;
            isConnected.value = false;
            isConnecting.value = false;
            devices.clear();
            stopDevicePoller();
        }
    };

    const scan = async () => {
        if (!bridge || !isConnected.value) return;

        isScanning.value = true;
        try {
            await bridge.scanBus();
            setTimeout(() => {
                isScanning.value = false;
            }, 3000);
        } catch (e: any) {
            errorMsg.value = `Scan failed: ${e.message}`;
            isScanning.value = false;
        }
    };


    let pollerInterval: any = null;
    const startDevicePoller = () => {
        if (pollerInterval) clearInterval(pollerInterval);
        pollerInterval = setInterval(() => {
            if (!bridge) return;
            const all = bridge.getAllDevices();
            all.forEach(dev => {
                if (!devices.has(dev.longAddress)) {
                    devices.set(dev.longAddress, dev);
                }
            });
        }, 1000);
    };

    const stopDevicePoller = () => {
        if (pollerInterval) clearInterval(pollerInterval);
        pollerInterval = null;
    };

    return {
        isConnected,
        isConnecting,
        isScanning,
        devices,
        errorMsg,
        connect,
        disconnect,
        scan
    };
}