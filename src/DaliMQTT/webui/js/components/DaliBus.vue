<!--
  - Copyright (c) 2026 Alice-Trade Inc.
  - SPDX-License-Identifier: GPL-3.0-or-later
  -->
<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
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
    const sortedDevices: DaliDevice[] = devicesRes.data.sort((a: DaliDevice, b: DaliDevice) => a.short_address - b.short_address);
    devices.value = sortedDevices;

    const names: DeviceNames = namesRes.data;
    sortedDevices.forEach(device => {
      if (!names[device.long_address]) names[device.long_address] = "";
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
  const intervalId = setInterval(async () => {
    try {
      const res = await api.getDaliStatus();
      if (res.data.status === 'idle') {
        clearInterval(intervalId);
        message.value = successMessage;
        await loadData();
        actionInProgress.value = '';
        setTimeout(() => { if (message.value === successMessage) message.value = ''; }, 3000);
      } else {
        let statusText = res.data.status;
        message.value = `Executing: ${statusText}...`;
      }
    } catch (e) {
      clearInterval(intervalId);
      message.value = `Error checking status.`;
      isError.value = true;
      actionInProgress.value = '';
    }
  }, 2000);
};

const runAction = async (action: 'scan' | 'init' | 'save', asyncFn: () => Promise<any>, successMessage: string, isAsyncDali: boolean = false) => {
  actionInProgress.value = action;
  message.value = `Executing: ${action}...`;
  isError.value = false;

  try {
    await asyncFn();
  } catch (e) {
    message.value = `An error occurred during execution: ${action}.`;
    isError.value = true;
  } finally {
    if (!isError.value) {
      if (isAsyncDali) {
        actionInProgress.value = action;
        pollStatus(successMessage);
      } else {
        message.value = successMessage;
        actionInProgress.value = '';
        setTimeout(() => {
          if (message.value === successMessage) message.value = '';
        }, 3000);
      }
    } else {
      actionInProgress.value = '';
    }
  }
};

const handleScan = () => runAction('scan', api.daliScan, 'Scan completed!', true);

const handleInitialize = () => {
  if (!confirm('This action will assign new short addresses to uninitialized devices on the bus. This is irreversible. Are you sure?')) {
    return;
  }
  runAction('init', api.daliInitialize, 'Initialization completed!', true);
};

const handleSaveChanges = () => {
  runAction('save', () => api.saveDaliNames(deviceNames.value), 'Names saved successfully!').then(() => {
    if (!isError.value) {
      pristineDeviceNames.value = JSON.parse(JSON.stringify(deviceNames.value));
    }
  });
};

const handleDiscardChanges = () => {
  deviceNames.value = JSON.parse(JSON.stringify(pristineDeviceNames.value));
};

onMounted(loadData);
</script>

<template>
  <article :aria-busy="loading || !!actionInProgress">
    <header>
      <h3>DALI Bus Management</h3>
      <p>Bus Management</p>
    </header>

    <div class="grid">
      <button @click="handleScan" :disabled="!!actionInProgress" :aria-busy="actionInProgress === 'scan'">Scan Bus</button>
      <button @click="handleInitialize" :disabled="!!actionInProgress" :aria-busy="actionInProgress === 'init'" class="contrast">Initialize New Devices</button>
    </div>

    <p v-if="message" :style="{ color: isError ? 'var(--pico-color-red-500)' : 'var(--pico-color-green-500)' }">{{ message }}</p>

    <div v-if="!loading">
      <div v-if="devices.length === 0" class="empty-state">
        <p><strong>No devices found on DALI bus.</strong></p>
        <p>Try scanning the bus or initializing new ballasts if connected.</p>
      </div>

      <div v-else>
        <div class="save-bar" v-if="isDirty">
          <span>You have unsaved name changes.</span>
          <div class="grid">
            <button class="secondary outline" @click="handleDiscardChanges" :disabled="actionInProgress === 'save'">Discard</button>
            <button @click="handleSaveChanges" :aria-busy="actionInProgress === 'save'">Save Names</button>
          </div>
        </div>

        <h4 v-if="gears.length > 0">Luminaires ({{ gears.length }})</h4>
        <div class="devices-grid" v-if="gears.length > 0">
          <div v-for="device in gears" :key="device.long_address" class="device-card">
            <header class="card-header">
              <div>
                <strong>Device {{ device.short_address }}</strong>
                <small class="long-address-text">{{ device.long_address }}</small>
              </div>
            </header>
            <div class="card-body">
              <label :for="`name-${device.long_address}`">Name</label>
              <input type="text" :id="`name-${device.long_address}`" v-model="deviceNames[device.long_address]" placeholder="e.g., Office Light 1" />
            </div>
          </div>
        </div>

        <div v-if="inputs.length > 0">
          <hr/>
          <h4>Input Devices: ({{ inputs.length }})</h4>
          <div class="devices-grid">
            <div v-for="device in inputs" :key="device.long_address" class="device-card input-card">
              <header class="card-header input-header">
                <div>
                  <strong>Device {{ device.short_address }}</strong> (Input)
                  <small class="long-address-text">{{ device.long_address }}</small>
                </div>
              </header>
              <div class="card-body">
                <label :for="`name-${device.long_address}`">Name</label>
                <input type="text" :id="`name-${device.long_address}`" v-model="deviceNames[device.long_address]" placeholder="e.g., Switch 1" />
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </article>
</template>

<style scoped>
.long-address-text {
  color: var(--pico-muted-color);
  font-family: monospace;
  font-size: 0.8em;
  display: block;
  margin-top: 0.2rem;
}
.empty-state {
  text-align: center;
  padding: 2rem;
  border: 2px dashed var(--pico-muted-border-color);
  border-radius: var(--pico-border-radius);
  margin-top: 1rem;
}
.save-bar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 1rem;
  background-color: var(--pico-card-background-color);
  border: 1px solid var(--pico-card-border-color);
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
  grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
  gap: 1.5rem;
  margin-bottom: 2rem;
}
.device-card {
  background-color: var(--pico-card-background-color);
  border: 1px solid var(--pico-card-border-color);
  border-radius: var(--pico-border-radius);
  box-shadow: var(--pico-card-box-shadow);
  display: flex;
  flex-direction: column;
}
.card-header {
  padding: 1rem 1.25rem;
  border-bottom: 1px solid var(--pico-card-border-color);
  background-color: var(--pico-table-header-background);
}
.input-header {
  background-color: var(--pico-muted-background-color);
}
.card-body {
  padding: 1.25rem;
  flex-grow: 1;
}
.card-body label {
  margin-bottom: 0.25rem;
  font-weight: bold;
  color: var(--pico-secondary);
  font-size: 0.9em;
}
</style>
