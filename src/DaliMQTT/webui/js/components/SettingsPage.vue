<!--
  - Copyright (c) 2026 Alice-Trade Inc.
  - SPDX-License-Identifier: GPL-3.0-or-later
  -->

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue';
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
  hass_discovery_prefix?: string;
}

interface OtaState {
  installed_version: string;
  latest_version: string;
  update_available: boolean;
  release_url: string;
  is_updating: boolean;
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
  hass_discovery_prefix: 'homeassistant',
});

const otaInfo = ref<OtaState>({
  installed_version: '',
  latest_version: '',
  update_available: false,
  release_url: '',
  is_updating: false,
});

const daliPollSeconds = ref(200.0);
const loading = ref(true);
const message = ref('');
const isError = ref(false);

const otaChecking = ref(false);
const otaUpdating = ref(false);
const otaStatusText = ref('');
const customOtaUrl = ref('');
let otaProgressTimer: any = null;

const tabs = [
  { id: 'wifi', label: 'WiFi', icon: '📡' },
  { id: 'mqtt', label: 'MQTT', icon: '🌐' },
  { id: 'dali', label: 'DALI Bus', icon: '💡' },
  { id: 'system', label: 'System', icon: '⚙️' },
  { id: 'logging', label: 'Maintenance', icon: '🔄' }
];
const activeTab = ref(tabs[0].id);
const networkType = ref('Wi-Fi');

const loadConfig = async () => {
  loading.value = true;
  message.value = '';
  try {
    const [cfgRes, infoRes] = await Promise.all([api.getConfig(), api.getInfo()]);
    config.value = cfgRes.data;
    networkType.value = infoRes.data.network_type || 'Wi-Fi';
    if (config.value.dali_poll_interval_ms) {
      daliPollSeconds.value = config.value.dali_poll_interval_ms / 1000.0;
    }
    if (infoRes.data.ota) {
      otaInfo.value = infoRes.data.ota;
      if (otaInfo.value.is_updating) {
        startOtaPolling();
      }
    }
  } catch (e) {
    message.value = 'Failed to load configuration.';
    isError.value = true;
  } finally {
    loading.value = false;
  }
};

const selectedFile = ref<File | null>(null);
const uploadProgress = ref(0);
const isUploadingFile = ref(false);

const handleFileSelect = (event: Event) => {
  const target = event.target as HTMLInputElement;
  if (target.files && target.files.length > 0) {
    const file = target.files[0];
    if (!file.name.endsWith('.bin')) {
      alert('Please select a valid compiled .bin file!');
      target.value = '';
      selectedFile.value = null;
      return;
    }
    selectedFile.value = file;
  }
};

const handleUploadBinFile = async () => {
  if (!selectedFile.value) return;

  const sizeKb = (selectedFile.value.size / 1024).toFixed(1);
  if (!confirm(`Flash "${selectedFile.value.name}" (${sizeKb} KB) directly to ESP32?`)) {
    return;
  }

  isUploadingFile.value = true;
  uploadProgress.value = 0;
  otaStatusText.value = 'Uploading binary file to device...';

  try {
    await api.uploadFirmwareFile(selectedFile.value, (pct) => {
      uploadProgress.value = pct;
      if (pct < 100) {
        otaStatusText.value = `Uploading to ESP32: ${pct}%`;
      } else {
        otaStatusText.value = 'Verifying and writing flash...';
      }
    });

    otaStatusText.value = 'Flashing complete! Bridge is restarting...';
    waitForDeviceReboot();
  } catch (e: any) {
    isUploadingFile.value = false;
    alert(e.response?.data || 'Failed to flash file. Check serial log.');
    otaStatusText.value = 'Upload failed.';
  }
};

const handleCheckOta = async () => {
  otaChecking.value = true;
  otaStatusText.value = 'Checking for updates...';
  try {
    await api.checkOta();
    let checks = 0;
    const pollInterval = setInterval(async () => {
      checks++;
      try {
        const infoRes = await api.getInfo();
        if (infoRes.data.ota) {
          otaInfo.value = infoRes.data.ota;
        }
        if (!otaInfo.value.is_updating || checks >= 4) {
          clearInterval(pollInterval);
          otaChecking.value = false;
          otaStatusText.value = otaInfo.value.update_available
              ? `Update available: ${otaInfo.value.latest_version}`
              : 'Firmware is up to date.';
        }
      } catch {
        clearInterval(pollInterval);
        otaChecking.value = false;
      }
    }, 1500);
  } catch (e) {
    otaChecking.value = false;
    otaStatusText.value = 'Failed to request update check.';
  }
};

const handleStartOta = async (useCustomUrl: boolean = false) => {
  const targetUrl = useCustomUrl ? customOtaUrl.value : '';
  const promptTarget = useCustomUrl ? targetUrl : otaInfo.value.latest_version || 'latest version';

  if (!confirm(`Flash firmware and WebUI assets (${promptTarget})? The bridge will reboot when completed.`)) {
    return;
  }

  otaUpdating.value = true;
  otaStatusText.value = 'Initiating download and flash procedure...';

  try {
    await api.triggerSystemOta(targetUrl || undefined);
    startOtaPolling();
  } catch (e) {
    otaUpdating.value = false;
    otaStatusText.value = 'Failed to start firmware update. Check URL and connectivity.';
  }
};

const startOtaPolling = () => {
  if (otaProgressTimer) clearInterval(otaProgressTimer);
  otaUpdating.value = true;
  let offlineStrikes = 0;

  otaProgressTimer = setInterval(async () => {
    try {
      const res = await api.getInfo();
      if (res.data.ota) {
        otaInfo.value = res.data.ota;
      }
      if (otaInfo.value.is_updating) {
        otaStatusText.value = 'Writing firmware & WebUI filesystem to flash...';
      } else {
        clearInterval(otaProgressTimer);
        otaStatusText.value = 'Update finalized successfully! Device is rebooting...';
        waitForDeviceReboot();
      }
    } catch {
      offlineStrikes++;
      otaStatusText.value = `Device is restarting with new firmware (attempting reconnect ${offlineStrikes})...`;
      if (offlineStrikes >= 3) {
        waitForDeviceReboot();
      }
    }
  }, 2000);
};

const waitForDeviceReboot = () => {
  if (otaProgressTimer) clearInterval(otaProgressTimer);
  const reconnectInterval = setInterval(async () => {
    try {
      await api.getInfo();
      clearInterval(reconnectInterval);
      otaStatusText.value = 'Device is online! Reloading interface...';
      setTimeout(() => window.location.reload(), 1500);
    } catch {
    }
  }, 3000);
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
  if (networkType.value !== 'Ethernet' && !config.value.wifi_ssid) {
    activeTab.value = 'wifi';
    message.value = 'SSID is required for Wi-Fi.';
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

  if (!confirm('Save settings and apply changes?')) {
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
    message.value = response.data.message || 'Settings saved successfully!';
    isError.value = false;
  } catch (e) {
    message.value = 'Failed to save configuration.';
    isError.value = true;
  } finally {
    loading.value = false;
  }
};

onMounted(loadConfig);
onUnmounted(() => {
  if (otaProgressTimer) clearInterval(otaProgressTimer);
});
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
            <div v-if="config.hass_discovery_enabled" style="margin-top: 0.5rem; margin-left: 1rem;">
              <label for="hass_prefix">Discovery Prefix</label>
              <input type="text" id="hass_prefix" v-model="config.hass_discovery_prefix" placeholder="homeassistant">
              <small>Default prefix is "homeassistant". Change only if modified in HA configuration.yaml.</small>
            </div>
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
            <h4 class="section-title">Maintenance & Updates</h4>

            <!-- Syslog -->
            <div class="card-like">
              <label for="syslog_enabled">
                <input type="checkbox" id="syslog_enabled" role="switch" v-model="config.syslog_enabled" />
                <strong>Enable Remote Syslog</strong>
              </label>

              <label for="syslog_server" style="margin-top: 1rem;">Syslog Server Address</label>
              <input type="text" id="syslog_server" v-model="config.syslog_server" placeholder="e.g., 192.168.1.100" :disabled="!config.syslog_enabled">
              <small>UDP port 514 RFC-5424 stream.</small>
            </div>

            <div class="card-like ota-dashboard" style="margin-top: 1.5rem;">
              <header class="ota-header">
                <div>
                  <strong>Firmware Updates</strong>
                </div>
                <div class="version-badges">
                  <span class="badge badge-installed">Current: {{ otaInfo.installed_version || 'v' + config.client_id }}</span>
                  <span v-if="otaInfo.update_available" class="badge badge-update">Update: {{ otaInfo.latest_version }}</span>
                  <span v-else-if="otaInfo.latest_version" class="badge badge-uptodate">Up to date</span>
                </div>
              </header>

              <div v-if="otaUpdating || otaInfo.is_updating" class="ota-progress-zone">
                <label>{{ otaStatusText || 'Flashing updates to memory...' }}</label>
                <progress></progress>
                <small class="muted-warning">⚠️ Do NOT disconnect power or reset the bridge during installation!</small>
              </div>

              <div v-else class="ota-actions-grid">
                <div class="grid">
                  <button type="button"
                          class="secondary outline"
                          @click="handleCheckOta"
                          :disabled="otaChecking || loading"
                          :aria-busy="otaChecking">
                    Check for Updates
                  </button>
                  <button v-if="otaInfo.update_available"
                          type="button"
                          class="contrast"
                          @click="() => handleStartOta(false)"
                          :disabled="otaUpdating || loading">
                    Install {{ otaInfo.latest_version }}
                  </button>
                </div>

                <div v-if="otaStatusText" class="ota-status-info">
                  <span>{{ otaStatusText }}</span>
                  <a v-if="otaInfo.release_url" :href="otaInfo.release_url" target="_blank" rel="noopener noreferrer" class="release-link">
                    Release Notes ↗
                  </a>
                </div>
              </div>

              <div class="ota-config-sub">
                <label for="ota_url">OTA Manifest URL</label>
                <input type="text" id="ota_url" v-model="config.ota_url" placeholder="https://api.github.com/repos/.../releases/latest">
                <small>JSON manifest endpoint for automated background checks.</small>
              </div>

              <details class="ota-advanced-details">
                <summary>Upload Firmware using .bin file</summary>
                <div class="advanced-body">
                  <label for="bin_file_input">Select Firmware Binary (.bin)</label>
                  <input type="file"
                         id="bin_file_input"
                         accept=".bin"
                         @change="handleFileSelect"
                         :disabled="isUploadingFile || otaUpdating">

                  <div v-if="selectedFile" class="file-summary-card">
                    <div class="file-info-row">
                      <span><strong>File:</strong> {{ selectedFile.name }}</span>
                      <span><strong>Size:</strong> {{ (selectedFile.size / 1024).toFixed(1) }} KB</span>
                    </div>

                    <div v-if="isUploadingFile" style="margin-top: 0.75rem;">
                      <progress :value="uploadProgress" max="100"></progress>
                      <small style="text-align: center; display: block;">{{ otaStatusText }}</small>
                    </div>

                    <button v-else
                            type="button"
                            class="contrast manual-install-btn"
                            @click="handleUploadBinFile"
                            :disabled="isUploadingFile">
                      Flash {{ selectedFile.name }}
                    </button>
                  </div>
                </div>
              </details>
            </div>

          </section>

        </div>
      </div>

      <footer class="form-actions">
        <button type="submit" :disabled="loading || otaUpdating">
          {{ isProvisioning ? 'Save and Complete Setup' : 'Save Settings' }}
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
  padding: 1.25rem;
  border: 1px solid var(--pico-muted-border-color);
  border-radius: var(--pico-border-radius);
  background-color: var(--pico-form-element-background-color);
  margin-bottom: 1rem;
}

.ota-dashboard {
  background: var(--pico-card-background-color);
  border: 1px solid var(--pico-card-border-color);
}

.ota-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  margin-bottom: 1.25rem;
  gap: 1rem;
}

.version-badges {
  display: flex;
  gap: 0.5rem;
  flex-wrap: wrap;
}

.badge {
  font-size: 0.75rem;
  font-weight: 600;
  padding: 0.25rem 0.6rem;
  border-radius: 12px;
  font-family: monospace;
}

.badge-installed {
  background: var(--pico-muted-background-color);
  color: var(--pico-color);
  border: 1px solid var(--pico-muted-border-color);
}

.badge-update {
  background: var(--pico-color-azure-500);
  color: #fff;
}

.badge-uptodate {
  background: var(--pico-color-green-500);
  color: #fff;
}

.ota-actions-grid {
  margin-bottom: 1.25rem;
}

.ota-actions-grid .grid {
  margin-bottom: 0.5rem;
}

.ota-status-info {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 0.9em;
  padding: 0.5rem 0;
}

.release-link {
  font-weight: 600;
  text-decoration: underline;
}

.ota-progress-zone {
  margin-bottom: 1.25rem;
  padding: 1rem;
  background: var(--pico-muted-background-color);
  border-radius: var(--pico-border-radius);
}

.muted-warning {
  color: var(--pico-color-amber-500);
  display: block;
  margin-top: 0.5rem;
  font-size: 0.8em;
}

.ota-config-sub {
  margin-top: 1.25rem;
  padding-top: 1rem;
  border-top: 1px dashed var(--pico-muted-border-color);
}

.ota-advanced-details {
  margin-top: 1rem;
  border: 1px solid var(--pico-muted-border-color);
  border-radius: var(--pico-border-radius);
  padding: 0.5rem 1rem;
}

.ota-advanced-details summary {
  font-size: 0.9em;
  color: var(--pico-muted-color);
  cursor: pointer;
}

.advanced-body {
  margin-top: 1rem;
}

.manual-install-btn {
  margin-top: 0.75rem;
  width: auto;
  font-size: 0.85em;
  padding: 0.4rem 0.8rem;
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

  .ota-header {
    flex-direction: column;
  }
}
.file-summary-card {
  margin-top: 1rem;
  padding: 1rem;
  background: var(--pico-form-element-background-color);
  border: 1px solid var(--pico-muted-border-color);
  border-radius: var(--pico-border-radius);
}

.file-info-row {
  display: flex;
  justify-content: space-between;
  font-size: 0.85em;
  font-family: monospace;
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