<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
import { api } from '../api';

const info = ref<Record<string, any> | null>(null);
const loading = ref(true);

const displayOrder = [
  'version', 'chip_model', 'chip_cores',
  'free_heap', 'wifi_status', 'mqtt_status', 'dali_status'
];

const formatKey = (key: string) => {
  return key.replace(/_/g, ' ').replace(/\b\w/g, l => l.toUpperCase());
};

const orderedInfo = computed(() => {
  const currentInfo = info.value;
  if (!currentInfo) {
    return [];
  }

  const ordered = displayOrder
      .filter(key => key in currentInfo)
      .map(key => ({ key: formatKey(key), value: currentInfo[key] }));

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
    <h3>System Information</h3>
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