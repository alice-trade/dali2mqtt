import axios from 'axios';

const apiClient = axios.create({
    baseURL: '/',
    timeout: 5000,
});

export function setAuth(user: string, pass: string) {
    const token = btoa(`${user}:${pass}`); // Кодируем в Base64
    apiClient.defaults.headers.common['Authorization'] = `Basic ${token}`;
}

export const api = {
    async getConfig() {
        return await apiClient.get('/api/config');
    },
    async getInfo() {
        return await apiClient.get('/api/info');
    },
    async saveConfig(configData: any) {
        return await apiClient.post('/api/config', configData);
    }
};