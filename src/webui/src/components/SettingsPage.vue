<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { api } from '../api';

const config = ref({
  wifi_ssid: '',
  wifi_password: '',
  mqtt_uri: '',
  mqtt_client_id: '',
  mqtt_base_topic: '',
  http_user: '',
  http_pass: '',
});

const loading = ref(true);
const message = ref('');
const isError = ref(false);

const loadConfig = async () => {
  loading.value = true;
  message.value = '';
  try {
    const response = await api.getConfig();
    config.value = { ...config.value, ...response.data };
  } catch (e) {
    message.value = 'Failed to load configuration.';
    isError.value = true;
  } finally {
    loading.value = false;
  }
};

const saveConfig = async () => {
  loading.value = true;
  message.value = '';
  const payload = { ...config.value };
  // Не отправляем пустые пароли, если они не были изменены
  if (!payload.wifi_password) delete payload.wifi_password;
  if (!payload.http_pass) delete payload.http_pass;

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
    <h3>Configuration</h3>
    <form @submit.prevent="saveConfig">
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
        <input type="text" id="mqtt_uri" v-model="config.mqtt_uri" placeholder="mqtt://user:pass@host:port" required>
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

      <button type="submit" :disabled="loading">Save and Restart</button>
    </form>
    <p v-if="message" :style="{ color: isError ? 'var(--pico-color-red-500)' : 'var(--pico-color-green-500)' }">{{ message }}</p>
  </article>
</template>