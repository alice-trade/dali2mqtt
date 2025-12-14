<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { api } from '../api';

interface DaliDeviceStatus {
  long_address: string;
  short_address: number;
  level: number;
  available: boolean;
  lamp_failure: boolean;
  dt?: number | null;
  is_input_device?: boolean;
}

const devices = ref<DaliDeviceStatus[]>([]);
const loading = ref(true);

const getDeviceState = (d: DaliDeviceStatus) => {
  if (!d.available) return 'Offline';
  if (d.is_input_device) return 'Ready';
  return d.level > 0 ? `ON (${Math.round(d.level/2.54)}%)` : 'OFF';
};

const getDeviceTypeStr = (d: DaliDeviceStatus) => {
  if (d.is_input_device) return 'Input Device';

  const dt = d.dt;
  if (dt === null || dt === undefined) return 'Standard';
  if (dt === 6) return 'LED (Type 6)';
  if (dt === 8) return 'RGB/W (Type 8)';
  return `Type ${dt}`;
};

const loadData = async () => {
  loading.value = true;
  try {
    const devicesRes = await api.getDaliDevices();
    devices.value = (devicesRes.data as DaliDeviceStatus[]).sort((a, b) => a.short_address - b.short_address);
  } catch (e) {
    console.error(e);
  } finally {
    loading.value = false;
  }
};

onMounted(loadData);
</script>

<template>
  <article :aria-busy="loading">
    <div class="header-with-button">
      <h3>Luminaires Status</h3>
      <button @click="loadData" :disabled="loading" class="outline secondary icon-button" title="Refresh">
        ⟳
      </button>
    </div>

    <div v-if="devices.length > 0" class="table-container">
      <table class="striped">
        <thead>
        <tr>
          <th>Addr</th>
          <th>Long Addr</th>
          <th>Type</th>
          <th>State</th>
          <th>Level</th>
        </tr>
        </thead>
        <tbody>
        <tr v-for="d in devices" :key="d.long_address">
          <td><strong>{{ d.short_address }}</strong></td>
          <td><small>{{ d.long_address }}</small></td>
          <td>
            <span v-if="d.is_input_device" class="badge-input">Input</span>
            <span v-else>{{ getDeviceTypeStr(d) }}</span>
          </td>
          <td>
              <span :class="{
                'badge-on': d.available && d.level > 0 && !d.lamp_failure && !d.is_input_device,
                'badge-off': d.available && d.level === 0 && !d.lamp_failure && !d.is_input_device,
                'badge-input-ready': d.available && d.is_input_device,
                'badge-fail': (d.lamp_failure && !d.is_input_device) || !d.available
              }">
                {{ getDeviceState(d) }}
              </span>
          </td>
          <td>{{ d.is_input_device ? '-' : d.level }}</td>
        </tr>
        </tbody>
      </table>
    </div>
    <div v-else-if="!loading">
      <p>No devices found.</p>
      <small>Go to DALI Control to scan bus.</small>
    </div>
  </article>
</template>

<style scoped>
.header-with-button {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 0;
}

.icon-button {
  padding: 0.25rem 0.75rem;
  font-size: 1.2rem;
  line-height: 1;
  width: auto;
}

.table-container {
  overflow-x: auto;
}

small {
  font-family: monospace;
  color: var(--pico-muted-color);
}

.badge-on {
  color: var(--pico-primary);
  font-weight: bold;
}
.badge-off {
  color: var(--pico-muted-color);
}
.badge-fail {
  color: var(--pico-color-red-500);
  font-weight: bold;
}
.badge-input-ready {
  color: var(--pico-color-azure-500);
  font-weight: bold;
}

.badge-input {
  font-size: 0.8em;
  background-color: var(--pico-color-azure-500);
  color: white;
  padding: 2px 6px;
  border-radius: 4px;
}
</style>