<!--
  - Copyright (c) 2026 Alice-Trade Inc.
  - SPDX-License-Identifier: GPL-3.0-or-later
  -->

<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { api } from '../api';

interface DaliBusConfig {
  enabled: boolean;
  rx_pin: number;
  tx_pin: number;
}

interface ConfigData {
  wifi_ssid: string;
  wifi_password?: string;
  mqtt_uri: string;
  mqtt_user?: string;
  mqtt_pass?: string;
  mqtt_ca_cert?: string;
  client_id: string;
  mqtt_base_topic: string;
  http_domain: string;
  http_user: string;
  http_pass?: string;
  syslog_server?: string;
  syslog_enabled?: boolean;
  ota_url?: string;
  dali_poll_interval_ms?: number;
  buses?: DaliBusConfig[];
  hass_discovery_enabled?: boolean;
}

const props = defineProps<{
  isProvisioning?: boolean;
}>();

const config = ref<ConfigData>({
  wifi_ssid: '',
  mqtt_uri: '',
  mqtt_user: '',
  mqtt_ca_cert: '',
  client_id: '',
  mqtt_base_topic: '',
  ota_url: '',
  http_domain: '',
  http_user: '',
  syslog_server: '',
  syslog_enabled: false,
  dali_poll_interval_ms: 200000,
  buses: [{ enabled: false, rx_pin: -1, tx_pin: -1 }],
  hass_discovery_enabled: false,
});

const daliPollSeconds = ref(200.0);
const loading = ref(true);
const message = ref('');
const isError = ref(false);

const loadConfig = async () => {
  loading.value = true;
  message.value = '';
  try {
    const response = await api.getConfig();
    config.value = response.data;
    if (config.value.dali_poll_interval_ms) {
      daliPollSeconds.value = config.value.dali_poll_interval_ms / 1000.0;
    }
  } catch (e) {
    message.value = 'Failed to load configuration.';
    isError.value = true;
  } finally {
    loading.value = false;
  }
};

const handleSystemOta = async () => {
  if (!confirm(`Start firmware update from ${config.value.ota_url}?`)) return;

  try {
    await api.triggerSystemOta(config.value.ota_url);

    alert("System update started! The device will reboot if successful. Please wait and reload the page.");
  } catch (e) {
    alert("Failed to start update. Check console/logs.");
    console.error(e);
  }
};

const handleCertFileUpload = (event: Event) => {
  const target = event.target as HTMLInputElement;
  if (target.files && target.files.length > 0) {
    const file = target.files[0];
    const reader = new FileReader();
    reader.onload = (e) => {
      if (e.target?.result) {
        config.value.mqtt_ca_cert = e.target.result as string;
      }
    };
    reader.readAsText(file);
  }
};

const saveConfig = async () => {
  if (!confirm('Save settings and reboot device?')) {
    return;
  }
  loading.value = true;
  message.value = '';

  const payload: ConfigData = { ...config.value };
  payload.dali_poll_interval_ms = Math.round(daliPollSeconds.value * 1000);

  if (!payload.wifi_password) delete payload.wifi_password;
  if (!payload.http_pass) delete payload.http_pass;
  if (!payload.mqtt_pass) delete payload.mqtt_pass;
  if (payload.mqtt_ca_cert === '***') delete payload.mqtt_ca_cert;

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
    <h3 v-if="isProvisioning">Initial Configuration</h3>
    <h3 v-else>System Settings</h3>

    <p v-if="isProvisioning" style="color: var(--pico-muted-color);">
      Please configure your device's WiFi and MQTT settings to complete the initial setup.
    </p>

    <form @submit.prevent="saveConfig">

      <!-- WiFi -->
      <details :open="isProvisioning">
        <summary><strong>WiFi Settings</strong></summary>
        <div class="details-content">
          <label for="ssid">SSID</label>
          <input type="text" id="ssid" v-model="config.wifi_ssid" required>

          <label for="wifi_pass">Password</label>
          <input type="password" id="wifi_pass" v-model="config.wifi_password" placeholder="Leave blank to keep unchanged">
        </div>
      </details>

      <!-- MQTT -->
      <details :open="isProvisioning">
        <summary><strong>MQTT Settings</strong></summary>
        <div class="details-content">
          <label for="mqtt_uri">Broker URI</label>
          <input type="text" id="mqtt_uri" v-model="config.mqtt_uri" placeholder="mqtts://host:8883" required>

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

          <label for="mqtt_ca_cert">TLS CA Certificate (PEM)</label>
          <textarea
              id="mqtt_ca_cert"
              v-model="config.mqtt_ca_cert"
              rows="4"
              placeholder="-----BEGIN CERTIFICATE----- ..."
              style="font-family: monospace; font-size: 0.8rem; white-space: pre;">
          </textarea>

          <label for="cert_upload">
            Upload Certificate File:
            <input type="file" id="cert_upload" @change="handleCertFileUpload" accept=".pem,.crt,.cer">
          </label>
          <small>Required for secure MQTT connections. Ensure the broker URI matches the certificate CN.</small>

          <label for="mqtt_base" style="margin-top: 1rem;">Base Topic</label>
          <input type="text" id="mqtt_base" v-model="config.mqtt_base_topic">

          <label for="hass_discovery">
            <input type="checkbox" id="hass_discovery" role="switch" v-model="config.hass_discovery_enabled" />
            Home Assistant Discovery
          </label>
          <small>Automatically publish configuration topics for Home Assistant.</small>
        </div>
      </details>

      <!-- DALI -->
      <details :open="isProvisioning">
        <summary><strong>DALI Bus Setup</strong></summary>
        <div class="details-content">
          <div v-for="(bus, index) in config.buses" :key="index">
            <label :for="`bus${index}_enabled`">
              <input type="checkbox" :id="`bus${index}_enabled`" role="switch" v-model="bus.enabled" />
              Enable DALI Driver {{ index + 1 }}
            </label>

            <div class="grid" v-if="bus.enabled">
              <div>
                <label :for="`bus${index}_rx`">Driver {{ index + 1 }} RX Pin (GPIO)</label>
                <input type="number" :id="`bus${index}_rx`" v-model.number="bus.rx_pin" min="-1" max="48" required>
              </div>
              <div>
                <label :for="`bus${index}_tx`">Driver {{ index + 1 }} TX Pin (GPIO)</label>
                <input type="number" :id="`bus${index}_tx`" v-model.number="bus.tx_pin" min="-1" max="48" required>
              </div>
            </div>
            <hr v-if="index < (config.buses?.length || 1) - 1">
          </div>

          <label for="poll_sec" style="margin-top: 1rem;">Bus Sync Interval (Seconds)</label>
          <input type="number" id="poll_sec" v-model="daliPollSeconds" min="0.5" step="0.1" required>
          <small>How often the bridge polls devices for sync status updates.</small>
        </div>
      </details>

      <!-- System & Web UI -->
      <details :open="isProvisioning">
        <summary><strong>System & Web UI</strong></summary>
        <div class="details-content">
          <label for="http_domain">WebUI mDNS Domain</label>
          <input type="text" id="http_domain" v-model="config.http_domain">
          <small>This value is used as the mDNS address (http://{{ config.http_domain || 'dalimqtt' }}.local).</small>

          <label for="client_id" style="margin-top: 1rem;">Client ID</label>
          <input type="text" id="client_id" v-model="config.client_id" required>
          <small>Used as ID for MQTT Client ID, Home Assistant Discovery, mDNS Device Name</small>

          <div class="grid" style="margin-top: 1rem;">
            <div>
              <label for="http_user">WebUI Username</label>
              <input type="text" id="http_user" v-model="config.http_user" required>
            </div>
            <div>
              <label for="http_pass">New WebUI Password</label>
              <input type="password" id="http_pass" v-model="config.http_pass" placeholder="Leave blank to keep unchanged">
            </div>
          </div>
        </div>
      </details>

      <!-- Logging & OTA -->
      <details>
        <summary><strong>Logging & Firmware Update</strong></summary>
        <div class="details-content">
          <label for="syslog_enabled">
            <input type="checkbox" id="syslog_enabled" role="switch" v-model="config.syslog_enabled" />
            Enable Remote Syslog
          </label>

          <label for="syslog_server">Syslog Server Address</label>
          <input type="text" id="syslog_server" v-model="config.syslog_server" placeholder="e.g., 192.168.1.100" :disabled="!config.syslog_enabled">
          <small>Logs will be sent to this server over UDP (port 514).</small>

          <hr>

          <label for="ota_url">Firmware URL</label>
          <input type="text" id="ota_url" v-model="config.ota_url" placeholder="http://server/firmware.bin">
          <small>Provide a URL to the binary file. Supports HTTP and HTTPS.</small>
          <button type="button" class="contrast" @click="handleSystemOta" :disabled="loading || !config.ota_url">
            Update from Server
          </button>
        </div>
      </details>

      <div style="margin-top: 2rem;">
        <button type="submit" :disabled="loading">{{ isProvisioning ? 'Save and Complete Setup' : 'Save and Reboot' }}</button>
        <p v-if="message" :style="{ color: isError ? 'var(--pico-color-red-500)' : 'var(--pico-color-green-500)' }">
          {{ message }}
        </p>
      </div>
    </form>
  </article>
</template>

<style scoped>
details {
  border: 1px solid var(--pico-muted-border-color);
  border-radius: var(--pico-border-radius);
  margin-bottom: 1rem;
  background-color: var(--pico-card-background-color);
}
summary {
  padding: 1rem;
  cursor: pointer;
  border-bottom: 1px solid transparent;
}
details[open] summary {
  border-bottom: 1px solid var(--pico-muted-border-color);
}
.details-content {
  padding: 1rem;
}
</style>
