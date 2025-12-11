/**
 * Utility functions for color conversion.
 */
export class ColorUtils {
    /**
     * Converts Kelvin temperature to Mireds.
     * @param kelvin Color temperature in Kelvin (e.g., 2700, 6500).
     * @returns Mireds value (1,000,000 / Kelvin).
     */
    static kelvinToMireds(kelvin: number): number {
        if (kelvin <= 0) return 0;
        return Math.round(1000000 / kelvin);
    }

    /**
     * Converts Mireds to Kelvin temperature.
     * @param mireds Color temperature in Mireds.
     * @returns Kelvin value.
     */
    static miredsToKelvin(mireds: number): number {
        if (mireds <= 0) return 0;
        return Math.round(1000000 / mireds);
    }
}