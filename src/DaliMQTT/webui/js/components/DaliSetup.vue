<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { api } from '../api';

const devices = ref<number[]>([]);
const loading = ref(true);
const actionInProgress = ref(false);
const message = ref('');
const isError = ref(false);

const loadDevices = async () => {
  loading.value = true;
  message.value = '';
  try {
    const response = await api.getDaliDevices();
    devices.value = response.data.sort((a: number, b: number) => a - b);
  } catch (e) {
    message.value = 'Failed to load DALI devices.';
    isError.value = true;
  } finally {
    loading.value = false;
  }
};

const handleScan = async () => {
  actionInProgress.value = true;
  message.value = 'Scanning DALI bus... This may take a moment.';
  isError.value = false;
  try {
    await api.daliScan();
    message.value = 'Scan complete!';
    await loadDevices();
  } catch (e) {
    message.value = 'An error occurred during the scan.';
    isError.value = true;
  } finally {
    actionInProgress.value = false;
  }
};

const handleInitialize = async () => {
  if (!confirm('This will assign new short addresses to uncommissioned devices on the bus. Are you sure?')) {
    return;
  }
  actionInProgress.value = true;
  message.value = 'Initializing DALI devices... This can take up to a minute.';
  isError.value = false;
  try {
    await api.daliInitialize();
    message.value = 'Initialization complete!';
    await loadDevices();
  } catch (e) {
    message.value = 'An error occurred during initialization.';
    isError.value = true;
  } finally {
    actionInProgress.value = false;
  }
};


onMounted(loadDevices);
</script>

<template>
  <article :aria-busy="loading || actionInProgress">
    <h3>DALI Bus Control</h3>
    <p>Manage and discover devices on the DALI bus.</p>

    <div class="grid">
      <button @click="handleScan" :disabled="actionInProgress">Scan Bus</button>
      <button @click="handleInitialize" :disabled="actionInProgress" class="contrast">Initialize New Devices</button>
    </div>

    <p v-if="message" :style="{ color: isError ? 'var(--pico-color-red-500)' : 'var(--pico-color-green-500)' }">{{ message }}</p>

    <div v-if="!loading">
      <h4>Discovered Devices ({{ devices.length }}/64)</h4>
      <div v-if="devices.length > 0" class="device-grid">
         <span v-for="device in devices" :key="device" class="device-tag">
            {{ device }}
        </span>
      </div>
      <p v-else>No devices found on the DALI bus. Try scanning or initializing.</p>
    </div>

  </article>
</template>

<style scoped>
.device-grid {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
  margin-top: 1rem;
}
.device-tag {
  background-color: var(--pico-primary-background);
  color: var(--pico-primary-inverse);
  padding: 0.25rem 0.75rem;
  border-radius: var(--pico-border-radius);
  font-family: monospace;
}
</style>