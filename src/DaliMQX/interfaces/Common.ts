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
    device_type?: number;
    gtin?: string;
    groups?: number[];
    min_level?: number;
    max_level?: number;
}

/**
 * Configuration options for the DaliBridge.
 */
export interface DaliBridgeOptions {
    /**
     * Base topic
     */
    baseTopic?: string;
}

export type EventCallback = (data: any) => void;