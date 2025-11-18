<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { api } from '../api';

interface ConfigData {
  wifi_ssid: string;
  wifi_password?: string;
  mqtt_uri: string;
  mqtt_user?: string;
  mqtt_pass?: string;
  mqtt_client_id: string;
  mqtt_base_topic: string;
  http_domain: string;
  http_user: string;
  http_pass?: string;
  syslog_server?: string;
  syslog_enabled?: boolean;
}

const config = ref<ConfigData>({
  wifi_ssid: '',
  mqtt_uri: '',
  mqtt_user: '',
  mqtt_client_id: '',
  mqtt_base_topic: '',
  http_domain: '',
  http_user: '',
  syslog_server: '',
  syslog_enabled: false,
});

const loading = ref(true);
const message = ref('');
const isError = ref(false);

const loadConfig = async () => {
  loading.value = true;
  message.value = '';
  try {
    const response = await api.getConfig();
    config.value = response.data;
  } catch (e) {
    message.value = 'Failed to load configuration.';
    isError.value = true;
  } finally {
    loading.value = false;
  }
};

const saveConfig = async () => {
  if (!confirm('Сохранить настройки и перезагрузить устройство?')) {
        return;
  }
  loading.value = true;
  message.value = '';

  const payload: ConfigData = { ...config.value };


  if (!payload.wifi_password) {
    delete payload.wifi_password;
  }
  if (!payload.http_pass) {
    delete payload.http_pass;
  }
  if (!payload.mqtt_pass) {
    delete payload.mqtt_pass;
  }

  try {
    const response = await api.saveConfig(payload);
    message.value = response.data.message || 'Settings saved successfully! Device is restarting...';
    isError.value = false;
  } catch (e) {
    message.value = 'Failed to save configuration.';
    isError.value = true;
  } finally {
    loading.value = false;
  }
};

onMounted(loadConfig);
</script>

<template>
  <article :aria-busy="loading">
    <h3>Settings</h3>
    <form @submit.prevent="saveConfig">
      <fieldset>
        <legend>General Settings</legend>
        <label for="cid">WebUI mDNS Domain</label>
        <input type="text" id="cid" v-model="config.http_domain">
        <small>This value is used as the mDNS address (http://{{ config.http_domain }}.local).</small>

      </fieldset>
      <fieldset>
        <legend>WiFi Settings</legend>
        <label for="ssid">SSID</label>
        <input type="text" id="ssid" v-model="config.wifi_ssid" required>
        <label for="wifi_pass">Password</label>
        <input type="password" id="wifi_pass" v-model="config.wifi_password" placeholder="Leave blank to keep unchanged">
      </fieldset>

      <fieldset>
        <legend>MQTT Settings</legend>
        <label for="mqtt_uri">Broker URI</label>
        <input type="text" id="mqtt_uri" v-model="config.mqtt_uri" placeholder="mqtt://host:port" required>
        <div class="grid">
          <div>
            <label for="mqtt_user">Username</label>
            <input type="text" id="mqtt_user" v-model="config.mqtt_user">
          </div>
          <div>
            <label for="mqtt_pass">Password</label>
            <input type="password" id="mqtt_pass" v-model="config.mqtt_pass" placeholder="Leave blank to keep unchanged">
          </div>
        </div>
        <label for="mqtt_cid">Client ID</label>
        <input type="text" id="mqtt_cid" v-model="config.mqtt_client_id">
        <label for="mqtt_base">Base Topic</label>
        <input type="text" id="mqtt_base" v-model="config.mqtt_base_topic">
      </fieldset>

      <fieldset>
        <legend>Web UI Authentication</legend>
        <label for="http_user">Username</label>
        <input type="text" id="http_user" v-model="config.http_user" required>
        <label for="http_pass">New Password</label>
        <input type="password" id="http_pass" v-model="config.http_pass" placeholder="Leave blank to keep unchanged">
      </fieldset>

      <fieldset>
        <legend>Logging</legend>
        <label for="syslog_enabled">
          <input type="checkbox" id="syslog_enabled" role="switch" v-model="config.syslog_enabled" />
                  Enable Remote Syslog
        </label>
        <label for="syslog_server">Syslog Server Address</label>
        <input type="text" id="syslog_server" v-model="config.syslog_server" placeholder="e.g., 192.168.1.100" :disabled="!config.syslog_enabled">
        <small>Logs will be sent to this server over UDP (port 514).</small>
      </fieldset>

      <button type="submit" :disabled="loading">Save and Reboot</button>
    </form>
    <p v-if="message" :style="{ color: isError ? 'var(--pico-color-red-500)' : 'var(--pico-color-green-500)' }">{{ message }}</p>
  </article>
</template>