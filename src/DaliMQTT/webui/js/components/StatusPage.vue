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
  if (!currentInfo) {
    return [];
  }

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


const loadInfo = async () => {
  loading.value = true;
  try {
    const response = await api.getInfo();
    info.value = response.data;
  } catch (e) {
    console.error(e);
  } finally {
    loading.value = false;
  }
};

onMounted(loadInfo);
</script>

<template>
  <article :aria-busy="loading">
    <div class="header-with-button">
      <h3>Status</h3>
      <button @click="loadInfo" :disabled="loading" class="no-outlined-button icon-button" title="Refresh status">
        <svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M21.5 2v6h-6"></path><path d="M2.5 22v-6h6"></path><path d="M2 11.5a10 10 0 0 1 18.8-4.3l-2.6 1.4a6 6 0 0 0-11.2 4.9"></path><path d="M22 12.5a10 10 0 0 1-18.8 4.3l2.6-1.4a6 6 0 0 0 11.2-4.9"></path>
        </svg>
      </button>
    </div>
    <div v-if="info">
      <table>
        <tbody>
        <tr v-for="item in orderedInfo" :key="item.key">
          <th scope="row">{{ item.key }}</th>
          <td>{{ item.value }}</td>
        </tr>
        </tbody>
      </table>
    </div>
    <p v-else-if="!loading">Failed to load system information.</p>
  </article>
</template>

<style scoped>
.header-with-button {
  display: flex;
  justify-content: space-between;
  align-items: center;
}


.icon-button {
  padding: 0.5rem;
  width: auto;
  display: flex;
  align-items: center;
  justify-content: center;
}

.icon-button svg {
  width: 1em;
  height: 1em;
  font-size: 1.25rem;
}

.no-outlined-button {
  --pico-background-color: transparent;
  --pico-border-color: transparent;
  --pico-box-shadow: none;
  color: var(--pico-secondary);
}

.no-outlined-button:active {
  transform: scale(0.95);
}
</style>