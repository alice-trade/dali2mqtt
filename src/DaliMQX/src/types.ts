export interface DaliState {
    state?: 'ON' | 'OFF';
    brightness?: number; // 0-254
    color_temp?: number; // Mireds
    color?: {
        r: number;
        g: number;
        b: number;
    };
    status_byte?: number;
    available?: boolean;
}

/**
 * Configuration options for the DaliBridge.
 */
export interface DaliBridgeOptions {
    /**
     * The base topic configured in the ESP32 firmware.
     */
    baseTopic?: string;
}

export type EventCallback = (data: any) => void;