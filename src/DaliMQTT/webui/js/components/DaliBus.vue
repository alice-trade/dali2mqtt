<!--
  - Copyright (c) 2026 Alice-Trade Inc.
  - SPDX-License-Identifier: GPL-3.0-or-later
  -->
<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed } from 'vue';
import { api } from '../api';

interface DaliDevice {
  long_address: string;
  short_address: number;
  type: 'gear' | 'input';
}

type DeviceNames = Record<string, string>;

const devices = ref<DaliDevice[]>([]);
const deviceNames = ref<DeviceNames>({});
const pristineDeviceNames = ref<DeviceNames>({});

const loading = ref(true);
const actionInProgress = ref('');
const message = ref('');
const isError = ref(false);
let pollTimer: any = null;

const gears = computed(() => devices.value.filter(d => d.type === 'gear'));
const inputs = computed(() => devices.value.filter(d => d.type === 'input'));

const isDirty = computed(() => {
  return JSON.stringify(deviceNames.value) !== JSON.stringify(pristineDeviceNames.value);
});

const loadData = async () => {
  loading.value = true;
  message.value = '';
  isError.value = false;
  try {
    const [devicesRes, namesRes] = await Promise.all([
      api.getDaliDevices(),
      api.getDaliNames()
    ]);
    const sorted: DaliDevice[] = devicesRes.data.sort((a: DaliDevice, b: DaliDevice) => a.short_address - b.short_address);
    devices.value = sorted;

    const names: DeviceNames = namesRes.data;
    sorted.forEach(dev => {
      if (!names[dev.long_address]) names[dev.long_address] = '';
    });

    deviceNames.value = JSON.parse(JSON.stringify(names));
    pristineDeviceNames.value = JSON.parse(JSON.stringify(names));
  } catch (e) {
    message.value = 'Failed to load DALI data. Check device connection.';
    isError.value = true;
  } finally {
    loading.value = false;
  }
};

const pollStatus = (successMessage: string) => {
  if (pollTimer) clearInterval(pollTimer);
  pollTimer = setInterval(async () => {
    try {
      const res = await api.getDaliStatus();
      if (res.data.status === 'idle') {
        clearInterval(pollTimer);
        pollTimer = null;
        message.value = successMessage;
        actionInProgress.value = '';
        await loadData();
        setTimeout(() => { if (message.value === successMessage) message.value = ''; }, 3000);
      } else {
        message.value = `Bus operation in progress: ${res.data.status}...`;
      }
    } catch {
      clearInterval(pollTimer);
      pollTimer = null;
      actionInProgress.value = '';
      message.value = 'Error querying bus status.';
      isError.value = true;
    }
  }, 1500);
};

const handleScan = async () => {
  actionInProgress.value = 'scan';
  message.value = 'Scanning bus for active devices...';
  isError.value = false;
  try {
    await api.daliScan();
    pollStatus('Bus scan completed successfully!');
  } catch {
    message.value = 'Failed to start bus scan.';
    isError.value = true;
    actionInProgress.value = '';
  }
};

const handleInitializeGears = async () => {
  if (!confirm('Run DALI commissioning for Control Gear? This assigns free short addresses to new luminaires.')) {
    return;
  }
  actionInProgress.value = 'init_gear';
  message.value = 'Commissioning luminaires...';
  isError.value = false;
  try {
    await api.daliInitialize();
    pollStatus('Luminaire commissioning complete!');
  } catch {
    message.value = 'Failed to start luminaire commissioning.';
    isError.value = true;
    actionInProgress.value = '';
  }
};

const handleInitializeInputs = async () => {
  if (!confirm('Run commissioning for Control Devices (sensors / switches)?')) {
    return;
  }
  actionInProgress.value = 'init_inp';
  message.value = 'Commissioning devices...';
  isError.value = false;
  try {
    await api.daliInitializeControlDevices();
    pollStatus('Control device commissioning complete!');
  } catch {
    message.value = 'Failed to start device commissioning.';
    isError.value = true;
    actionInProgress.value = '';
  }
};

const handleSaveNames = async () => {
  actionInProgress.value = 'save_names';
  message.value = 'Saving names...';
  isError.value = false;
  try {
    await api.saveDaliNames(deviceNames.value);
    pristineDeviceNames.value = JSON.parse(JSON.stringify(deviceNames.value));
    message.value = 'Device names saved!';
    setTimeout(() => { if (message.value === 'Device names saved!') message.value = ''; }, 3000);
  } catch {
    message.value = 'Failed to save names.';
    isError.value = true;
  } finally {
    actionInProgress.value = '';
  }
};

const handleDiscardNames = () => {
  deviceNames.value = JSON.parse(JSON.stringify(pristineDeviceNames.value));
};

onMounted(loadData);
onUnmounted(() => {
  if (pollTimer) clearInterval(pollTimer);
});
</script>

<template>
  <article :aria-busy="loading || !!actionInProgress">
    <header>
      <h3>DALI Bus Management</h3>
      <p>Bus Management</p>
    </header>

    <div class="grid">
      <button @click="handleScan" :disabled="!!actionInProgress" :aria-busy="actionInProgress === 'scan'">
        Scan Bus
      </button>
      <button @click="handleInitializeGears" :disabled="!!actionInProgress" :aria-busy="actionInProgress === 'init_gear'" class="contrast">
        Commission Luminaires
      </button>
      <button @click="handleInitializeInputs" :disabled="!!actionInProgress" :aria-busy="actionInProgress === 'init_inp'" class="outline">
        Commission Sensors (24-bit)
      </button>
    </div>

    <p v-if="message" class="status-msg" :class="{ 'error-msg': isError }">{{ message }}</p>

    <div v-if="!loading">
      <div v-if="devices.length === 0" class="empty-state">
        <p><strong>No devices found on DALI bus.</strong></p>
        <p>Try scanning the bus or initializing new ballasts if connected.</p>
      </div>

      <div v-else>
        <div class="save-bar" v-if="isDirty">
          <span>You have unsaved name modifications.</span>
          <div class="grid">
            <button class="secondary outline" @click="handleDiscardNames" :disabled="actionInProgress === 'save_names'">Discard</button>
            <button @click="handleSaveNames" :aria-busy="actionInProgress === 'save_names'">Save Names</button>
          </div>
        </div>

        <h4 v-if="gears.length > 0">Luminaires ({{ gears.length }})</h4>
        <div class="devices-grid" v-if="gears.length > 0">
          <div v-for="dev in gears" :key="dev.long_address" class="device-card">
            <header class="card-header">
              <strong>Short Addr: {{ dev.short_address }}</strong>
              <small class="long-addr-text">{{ dev.long_address }}</small>
            </header>
            <div class="card-body">
              <label :for="`name-${dev.long_address}`">Custom Name</label>
              <input type="text" :id="`name-${dev.long_address}`" v-model="deviceNames[dev.long_address]" placeholder="e.g. Living Room Spotlight" />
            </div>
          </div>
        </div>

        <div v-if="inputs.length > 0">
          <hr />
          <h4>Control Devices: ({{ inputs.length }})</h4>
          <div class="devices-grid">
            <div v-for="dev in inputs" :key="dev.long_address" class="device-card input-card">
              <header class="card-header input-header">
                <strong>Input Addr: {{ dev.short_address }}</strong>
                <small class="long-addr-text">{{ dev.long_address }}</small>
              </header>
              <div class="card-body">
                <label :for="`name-${dev.long_address}`">Custom Name</label>
                <input type="text" :id="`name-${dev.long_address}`" v-model="deviceNames[dev.long_address]" placeholder="e.g. Hallway PIR" />
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </article>
</template>

<style scoped>
.grid {
  margin-bottom: 1.5rem;
  gap: 1rem;
}
.status-msg {
  font-weight: bold;
  color: var(--pico-color-green-500);
}
.error-msg {
  color: var(--pico-color-red-500);
}
.empty-state {
  text-align: center;
  padding: 2.5rem;
  border: 2px dashed var(--pico-muted-border-color);
  border-radius: var(--pico-border-radius);
  margin-top: 1rem;
}
.save-bar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 1rem 1.25rem;
  background-color: var(--pico-card-background-color);
  border: 1px solid var(--pico-primary);
  border-radius: var(--pico-border-radius);
  margin-bottom: 1.5rem;
  position: sticky;
  top: 1rem;
  z-index: 10;
  box-shadow: var(--pico-box-shadow);
}
.save-bar .grid {
  margin-bottom: 0;
  gap: 0.5rem;
}
.devices-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 1.25rem;
  margin-bottom: 2rem;
}
.device-card {
  background-color: var(--pico-card-background-color);
  border: 1px solid var(--pico-card-border-color);
  border-radius: var(--pico-border-radius);
  display: flex;
  flex-direction: column;
}
.card-header {
  padding: 0.75rem 1rem;
  border-bottom: 1px solid var(--pico-card-border-color);
  display: flex;
  justify-content: space-between;
  align-items: baseline;
}
.input-header {
  background-color: var(--pico-muted-background-color);
}
.long-addr-text {
  font-family: monospace;
  color: var(--pico-muted-color);
}
.card-body {
  padding: 1rem;
}
.card-body label {
  font-size: 0.85em;
  margin-bottom: 0.25rem;
  color: var(--pico-muted-color);
}
.card-body input {
  margin-bottom: 0;
}
</style>
