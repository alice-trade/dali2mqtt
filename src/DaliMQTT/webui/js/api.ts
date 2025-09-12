import axios from 'axios';

const apiClient = axios.create({
    baseURL: '/',
    timeout: 30000,
});

export function setAuth(user: string, pass: string) {
    const token = btoa(`${user}:${pass}`);
    apiClient.defaults.headers.common['Authorization'] = `Basic ${token}`;
}

export const api = {
    // Config
    async getConfig() {
        return await apiClient.get('/api/config');
    },
    async saveConfig(configData: any) {
        return await apiClient.post('/api/config', configData);
    },
    // Info
    async getInfo() {
        return await apiClient.get('/api/info');
    },
    // DALI
    async getDaliDevices() {
        return await apiClient.get('/api/dali/devices');
    },
    async daliScan() {
        return await apiClient.post('/api/dali/scan');
    },
    async daliInitialize() {
        return await apiClient.post('/api/dali/initialize');
    }
};