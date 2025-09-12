<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { api } from '../api';

const info = ref<Record<string, any> | null>(null);
const loading = ref(true);

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
        <tr v-for="(value, key) in info" :key="key">
          <th scope="row">{{ key }}</th>
          <td>{{ value }}</td>
        </tr>
        </tbody>
      </table>
    </div>
    <p v-else-if="!loading">Failed to load system information.</p>
  </article>
</template>