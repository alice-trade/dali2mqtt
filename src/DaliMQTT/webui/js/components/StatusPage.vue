<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
import { api } from '../api';

const info = ref<Record<string, any> | null>(null);
const loading = ref(true);

const formatUptime = (totalSeconds: number) => {
  if (typeof totalSeconds !== 'number' || totalSeconds < 0) {
    return 'N/A';
  }

  const days = Math.floor(totalSeconds / 86400);
  totalSeconds %= 86400;
  const hours = Math.floor(totalSeconds / 3600);
  totalSeconds %= 3600;
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;

  const pad = (num: number) => String(num).padStart(2, '0');
  let result = `${pad(hours)}:${pad(minutes)}:${pad(seconds)}`;
  if (days > 0) {
    result = `${days} day${days > 1 ? 's' : ''}, ${result}`;
  }
  return result;
};

const displayOrder = [
  'version', 'chip_model', 'chip_cores', 'free_heap', 'uptime_seconds', 'firmware_verbosity_level',
  'wifi_status', 'mqtt_status', 'dali_status'
];

const formatKey = (key: string) => {
  if (key === 'uptime_seconds') return 'Uptime';
  return key.replace(/_/g, ' ').replace(/\b\w/g, l => l.toUpperCase());
};

const formatLogLevel = (level: number) => {
  const levels: Record<number, string> = {
          0: 'None',
          1: 'Error',
          2: 'Warning',
          3: 'Info',
          4: 'Debug',
          5: 'Verbose',
    };
  return levels[level] || `Unknown (${level})`;
};

const orderedInfo = computed(() => {
  const currentInfo = info.value;
  if (!currentInfo) return [];
  const ordered = displayOrder
      .filter(key => key in currentInfo)
      .map(key => ({
        key: formatKey(key),
        value: key === 'uptime_seconds' ? formatUptime(currentInfo[key]) :
            key === 'firmware_verbosity_level' ? formatLogLevel(currentInfo[key]) : currentInfo[key]
      }));
  const remaining = Object.entries(currentInfo)
      .filter(([key]) => !displayOrder.includes(key))
      .map(([key, value]) => ({ key: formatKey(key), value }));
  return [...ordered, ...remaining];
});


interface DaliDeviceStatus {
  long_address: string;
  short_address: number;
  level: number;
  available: boolean;
  lamp_failure: boolean;
  dt?: number | null;
}

const devices = ref<DaliDeviceStatus[]>([]);

const getDeviceState = (d: DaliDeviceStatus) => {
  if (!d.available) return 'Offline';
  if (d.lamp_failure) return 'Failure';
  return d.level > 0 ? `ON (${Math.round(d.level/2.54)}%)` : 'OFF';
};

const getDeviceTypeStr = (dt: number | undefined | null) => {
  if (dt === null || dt === undefined) return '-';
  if (dt === 6) return 'LED (6)';
  if (dt === 8) return 'RGB/W (8)';
  return `${dt}`;
};

const loadData = async () => {
  loading.value = true;
  try {
    const [infoRes, devicesRes] = await Promise.all([
      api.getInfo(),
      api.getDaliDevices()
    ]);
    info.value = infoRes.data;

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
  <div class="grid-container">
    <article :aria-busy="loading">
      <header>
        <h3>System Info</h3>
      </header>
      <div v-if="info">
        <table class="striped">
          <tbody>
          <tr v-for="item in orderedInfo" :key="item.key">
            <th scope="row">{{ item.key }}</th>
            <td style="text-align: right;">{{ item.value }}</td>
          </tr>
          </tbody>
        </table>
      </div>
      <p v-else-if="!loading">Failed to load system information.</p>
    </article>

    <article :aria-busy="loading">
      <header class="header-with-button">
        <h3>Luminaires Status</h3>
        <button @click="loadData" :disabled="loading" class="outline secondary icon-button" title="Refresh">
          ⟳
        </button>
      </header>

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
            <td>{{ getDeviceTypeStr(d.dt) }}</td>
            <td>
                <span :class="{
                  'badge-on': d.available && d.level > 0 && !d.lamp_failure,
                  'badge-off': d.available && d.level === 0 && !d.lamp_failure,
                  'badge-fail': d.lamp_failure || !d.available
                }">
                  {{ getDeviceState(d) }}
                </span>
            </td>
            <td>{{ d.level }}</td>
          </tr>
          </tbody>
        </table>
      </div>
      <div v-else-if="!loading">
        <p>No devices found.</p>
        <small>Go to DALI Control to scan bus.</small>
      </div>
    </article>
  </div>
</template>

<style scoped>
.grid-container {
  display: grid;
  grid-template-columns: 1fr;
  gap: 1.5rem;
}

@media (min-width: 992px) {
  .grid-container {
    grid-template-columns: 1fr 1.5fr;
  }
}

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
</style>