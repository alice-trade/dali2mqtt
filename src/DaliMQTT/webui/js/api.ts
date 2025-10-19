import axios from 'axios';

const apiClient = axios.create({
    baseURL: '/',
    timeout: 30000,
});

export function setAuth(user: string, pass: string) {
    const token = btoa(`${user}:${pass}`);
    apiClient.defaults.headers.common['Authorization'] = `Basic ${token}`;
    return token;
}

export function setAuthToken(token: string) {
    apiClient.defaults.headers.common['Authorization'] = `Basic ${token}`;
}

export function clearAuth() {
    delete apiClient.defaults.headers.common['Authorization'];
}

// Intercept 401 Unauthorized responses
apiClient.interceptors.response.use(
    (response) => response,
    (error) => {
        if (error.response && error.response.status === 401) {
            clearAuth();
            localStorage.removeItem('auth');
            window.location.reload();
        }
        return Promise.reject(error);
    }
);


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
    async getDaliStatus() {
        return await apiClient.get('/api/dali/status');
    },
    async daliInitialize() {
        return await apiClient.post('/api/dali/initialize');
    },
    async getDaliNames() {
        return await apiClient.get('/api/dali/names');
    },
    async saveDaliNames(names: Record<string, string>) {
        return await apiClient.post('/api/dali/names', names);
    },
    async getDaliGroups() {
        return await apiClient.get('/api/dali/groups');
    },
    async saveDaliGroups(assignments: Record<string, number[]>) {
        return await apiClient.post('/api/dali/groups', assignments);
    },
    async saveDaliScene(payload: { scene_id: number, levels: Record<string, number> }) {
        return await apiClient.post('/api/dali/scenes', payload);
    }
};