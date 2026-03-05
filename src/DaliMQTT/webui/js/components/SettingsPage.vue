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

// Настройка вкладок меню
const tabs = [
  { id: 'wifi', label: 'WiFi', icon: '📡' },
  { id: 'mqtt', label: 'MQTT', icon: '🌐' },
  { id: 'dali', label: 'DALI Bus', icon: '💡' },
  { id: 'system', label: 'System', icon: '⚙️' },
  { id: 'logging', label: 'Maintenance', icon: '🔄' }
];
const activeTab = ref(tabs[0].id);

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

const validateForm = (): boolean => {
  if (!config.value.wifi_ssid) {
    activeTab.value = 'wifi';
    message.value = 'SSID is required.';
    return false;
  }
  if (!config.value.mqtt_uri) {
    activeTab.value = 'mqtt';
    message.value = 'MQTT Broker URI is required.';
    return false;
  }
  if (!config.value.client_id) {
    activeTab.value = 'system';
    message.value = 'Client ID is required.';
    return false;
  }
  if (!config.value.http_user) {
    activeTab.value = 'system';
    message.value = 'WebUI Username is required.';
    return false;
  }
  if (!daliPollSeconds.value) {
    activeTab.value = 'dali';
    message.value = 'Bus Sync Interval is required.';
    return false;
  }
  return true;
};

const saveConfig = async () => {
  isError.value = true;

  if (!validateForm()) return;

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
    <header>
      <h3 v-if="isProvisioning" style="margin-bottom: 0;">Initial Configuration</h3>
      <h3 v-else style="margin-bottom: 0;">System Settings</h3>
      <p v-if="isProvisioning" style="color: var(--pico-muted-color); font-size: 0.9em; margin-top: 0.5rem; margin-bottom: 0;">
        Please configure your device's WiFi and MQTT settings to complete the initial setup.
      </p>
    </header>

    <form @submit.prevent="saveConfig">
      <div class="settings-layout">

        <!-- Sidebar -->
        <aside class="settings-sidebar">
          <nav>
            <ul>
              <li v-for="tab in tabs" :key="tab.id">
                <a href="#"
                   @click.prevent="activeTab = tab.id"
                   :class="{ active: activeTab === tab.id }">
                  <span class="tab-icon">{{ tab.icon }}</span>
                  <span class="tab-label">{{ tab.label }}</span>
                </a>
              </li>
            </ul>
          </nav>
        </aside>

        <!-- Content Area -->
        <div class="settings-content">

          <!-- WiFi -->
          <section v-show="activeTab === 'wifi'">
            <h4 class="section-title">WiFi Settings</h4>

            <label for="ssid">SSID <span class="required">*</span></label>
            <input type="text" id="ssid" v-model="config.wifi_ssid">

            <label for="wifi_pass">Password</label>
            <input type="password" id="wifi_pass" v-model="config.wifi_password" placeholder="Leave blank to keep unchanged">
          </section>

          <!-- MQTT -->
          <section v-show="activeTab === 'mqtt'">
            <h4 class="section-title">MQTT Settings</h4>

            <label for="mqtt_uri">Broker URI <span class="required">*</span></label>
            <input type="text" id="mqtt_uri" v-model="config.mqtt_uri" placeholder="mqtts://host:8883">

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

            <label for="mqtt_base" style="margin-top: 1.5rem;">Base Topic</label>
            <input type="text" id="mqtt_base" v-model="config.mqtt_base_topic">

            <label for="hass_discovery">
              <input type="checkbox" id="hass_discovery" role="switch" v-model="config.hass_discovery_enabled" />
              Home Assistant Discovery
            </label>
            <small>Automatically publish configuration topics for Home Assistant.</small>
          </section>

          <!-- DALI -->
          <section v-show="activeTab === 'dali'">
            <h4 class="section-title">DALI Bus Setup</h4>

            <div class="bus-card" v-for="(bus, index) in config.buses" :key="index">
              <label :for="`bus${index}_enabled`">
                <input type="checkbox" :id="`bus${index}_enabled`" role="switch" v-model="bus.enabled" />
                <strong>Enable DALI Driver {{ index + 1 }}</strong>
              </label>

              <div class="grid" v-if="bus.enabled" style="margin-top: 1rem;">
                <div>
                  <label :for="`bus${index}_rx`">RX Pin (GPIO)</label>
                  <input type="number" :id="`bus${index}_rx`" v-model.number="bus.rx_pin" min="-1" max="48">
                </div>
                <div>
                  <label :for="`bus${index}_tx`">TX Pin (GPIO)</label>
                  <input type="number" :id="`bus${index}_tx`" v-model.number="bus.tx_pin" min="-1" max="48">
                </div>
              </div>
            </div>

            <label for="poll_sec" style="margin-top: 1.5rem;">Bus Sync Interval (Seconds) <span class="required">*</span></label>
            <input type="number" id="poll_sec" v-model="daliPollSeconds" min="0.5" step="0.1">
            <small>How often the bridge polls devices for sync status updates.</small>
          </section>

          <!-- System & Web UI -->
          <section v-show="activeTab === 'system'">
            <h4 class="section-title">System & Web UI</h4>

            <label for="http_domain">WebUI mDNS Domain</label>
            <input type="text" id="http_domain" v-model="config.http_domain">
            <small>This value is used as the mDNS address (http://{{ config.http_domain || 'dalimqtt' }}.local).</small>

            <label for="client_id" style="margin-top: 1.5rem;">Client ID <span class="required">*</span></label>
            <input type="text" id="client_id" v-model="config.client_id">
            <small>Used as ID for MQTT Client ID, Home Assistant Discovery, mDNS Device Name.</small>

            <div class="grid" style="margin-top: 1.5rem;">
              <div>
                <label for="http_user">WebUI Username <span class="required">*</span></label>
                <input type="text" id="http_user" v-model="config.http_user">
              </div>
              <div>
                <label for="http_pass">New WebUI Password</label>
                <input type="password" id="http_pass" v-model="config.http_pass" placeholder="Leave blank to keep unchanged">
              </div>
            </div>
          </section>

          <!-- Logging & OTA -->
          <section v-show="activeTab === 'logging'">
            <h4 class="section-title">Logging & Firmware Update</h4>

            <div class="card-like">
              <label for="syslog_enabled">
                <input type="checkbox" id="syslog_enabled" role="switch" v-model="config.syslog_enabled" />
                Enable Remote Syslog
              </label>

              <label for="syslog_server" style="margin-top: 1rem;">Syslog Server Address</label>
              <input type="text" id="syslog_server" v-model="config.syslog_server" placeholder="e.g., 192.168.1.100" :disabled="!config.syslog_enabled">
              <small>Logs will be sent to this server over UDP (port 514).</small>
            </div>

            <div class="card-like" style="margin-top: 1.5rem;">
              <label for="ota_url">Firmware URL</label>
              <input type="text" id="ota_url" v-model="config.ota_url" placeholder="http://server/firmware.bin">
              <small style="display: block; margin-bottom: 1rem;">Provide a URL to the binary file. Supports HTTP and HTTPS.</small>
              <button type="button" class="contrast" @click="handleSystemOta" :disabled="loading || !config.ota_url" style="width: auto;">
                Update from Server
              </button>
            </div>
          </section>

        </div>
      </div>

      <!-- Action Bar  -->
      <footer class="form-actions">
        <button type="submit" :disabled="loading">
          {{ isProvisioning ? 'Save and Complete Setup' : 'Save and Reboot' }}
        </button>
        <p v-if="message" class="status-message" :class="{ 'error-msg': isError, 'success-msg': !isError }">
          {{ message }}
        </p>
      </footer>

    </form>
  </article>
</template>

<style scoped>
.settings-layout {
  display: grid;
  grid-template-columns: 240px 1fr;
  gap: 2rem;
  align-items: start;
}

.settings-sidebar nav ul {
  list-style: none;
  padding: 0;
  margin: 0;
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.settings-sidebar a {
  display: flex;
  align-items: center;
  padding: 0.75rem 1rem;
  border-radius: var(--pico-border-radius);
  color: var(--pico-h2-color);
  text-decoration: none;
  transition: all 0.2s ease-in-out;
  border: 1px solid transparent;
}

.settings-sidebar a:hover {
  background-color: var(--pico-muted-background-color);
}

.settings-sidebar a.active {
  background-color: var(--pico-primary-background);
  color: var(--pico-primary-inverse);
  font-weight: 600;
  border-color: var(--pico-primary);
  box-shadow: 0 4px 6px rgba(0,0,0,0.1);
}

.tab-icon {
  margin-right: 0.75rem;
  font-size: 1.2em;
}

.settings-content {
  min-height: 400px;
}

.section-title {
  margin-top: 0;
  margin-bottom: 1.5rem;
  padding-bottom: 0.5rem;
  border-bottom: 1px solid var(--pico-muted-border-color);
  color: var(--pico-h1-color);
}

.required {
  color: var(--pico-color-red-500);
}

.bus-card, .card-like {
  padding: 1rem;
  border: 1px solid var(--pico-muted-border-color);
  border-radius: var(--pico-border-radius);
  background-color: var(--pico-form-element-background-color);
  margin-bottom: 1rem;
}

.bus-card label {
  margin-bottom: 0;
}

.form-actions {
  margin-top: 2rem;
  padding-top: 1.5rem;
  border-top: 1px solid var(--pico-muted-border-color);
  display: flex;
  align-items: center;
  gap: 1.5rem;
  flex-wrap: wrap;
}

.form-actions button {
  width: auto;
  margin-bottom: 0;
}

.status-message {
  margin: 0;
  font-weight: bold;
}

.error-msg {
  color: var(--pico-color-red-500);
}

.success-msg {
  color: var(--pico-color-green-500);
}

@media (max-width: 768px) {
  .settings-layout {
    grid-template-columns: 1fr;
    gap: 1.5rem;
  }

  .settings-sidebar nav ul {
    flex-direction: row;
    overflow-x: auto;
    padding-bottom: 0.5rem;
  }

  .settings-sidebar a {
    white-space: nowrap;
    padding: 0.5rem 1rem;
  }

  .tab-label {
    display: none;
  }

  .tab-icon {
    margin-right: 0;
  }
}

@media (min-width: 480px) and (max-width: 768px) {
  .tab-label {
    display: inline;
  }
  .tab-icon {
    margin-right: 0.5rem;
  }
}
</style>