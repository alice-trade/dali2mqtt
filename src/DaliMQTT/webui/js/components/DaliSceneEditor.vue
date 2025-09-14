<script setup lang="ts">
import { ref } from 'vue';
import { api } from '../api';

const props = defineProps<{
  devices: number[],
  deviceNames: Record<string, string>
}>();

const selectedScene = ref(0);
const sceneLevels = ref<Record<string, number>>({});
const actionInProgress = ref(false);
const message = ref('');
const isError = ref(false);

const sceneOptions = Array.from({ length: 16 }, (_, i) => i);

const getDeviceDisplayName = (addr: number) => {
  return props.deviceNames[addr] || `Device ${addr}`;
};

const initSceneLevels = () => {
  const levels: Record<string, number> = {};
  props.devices.forEach(addr => {
    levels[addr] = 254;
  });
  sceneLevels.value = levels;
};

const handleSaveScene = async () => {
  actionInProgress.value = true;
  message.value = 'Saving scene configuration to DALI devices...';
  isError.value = false;

  const payload = {
    scene_id: selectedScene.value,
    levels: sceneLevels.value
  };

  try {
    await api.saveDaliScene(payload);
    message.value = `Scene ${selectedScene.value} saved successfully!`;
  } catch (e) {
    message.value = 'Failed to save scene.';
    isError.value = true;
  } finally {
    actionInProgress.value = false;
  }
};

const setAll = (level: number) => {
  props.devices.forEach(addr => {
    sceneLevels.value[addr] = level;
  });
};

initSceneLevels();
</script>

<template>
  <div>
    <h4>Scene Editor</h4>
    <p>Configure brightness levels for each device and save them to a scene. The settings are stored directly on the DALI ballasts.</p>

    <form @submit.prevent="handleSaveScene">
      <div class="grid">
        <div>
          <label for="scene-select">Select Scene to Edit</label>
          <select id="scene-select" v-model="selectedScene">
            <option v-for="scene in sceneOptions" :key="scene" :value="scene">Scene {{ scene }}</option>
          </select>
        </div>
        <div>
          <label>Set All Devices To:</label>
          <div class="grid">
            <button type="button" class="outline" @click="setAll(254)">Max</button>
            <button type="button" class="outline" @click="setAll(127)">Mid</button>
            <button type="button" class="outline" @click="setAll(0)">Off</button>
          </div>
        </div>
      </div>


      <h5>Device Levels for Scene {{ selectedScene }}</h5>
      <div class="device-list">
        <div v-for="addr in devices" :key="addr" class="device-item">
          <label :for="`level-${addr}`">{{ getDeviceDisplayName(addr) }}</label>
          <input type="range" min="0" max="254" step="1" :id="`level-${addr}`" v-model.number="sceneLevels[addr]">
          <span>{{ sceneLevels[addr] }}</span>
        </div>
      </div>

      <button type="submit" :disabled="actionInProgress" :aria-busy="actionInProgress">Save Scene {{ selectedScene }}</button>
      <p v-if="message" :style="{ color: isError ? 'var(--pico-color-red-500)' : 'var(--pico-color-green-500)' }">{{ message }}</p>
    </form>
  </div>
</template>

<style scoped>
.device-list {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
  gap: 1.5rem;
  margin-block: 1rem;
}
.device-item {
  display: grid;
  grid-template-columns: 1fr auto;
  align-items: center;
  gap: 1rem;
}
.device-item label {
  grid-column: 1 / 3;
  margin-bottom: 0;
}
.device-item input[type="range"] {
  margin-bottom: 0;
}
</style>