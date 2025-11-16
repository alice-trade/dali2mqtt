<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { api } from '../api';

interface DaliDevice {
  long_address: string;
  short_address: number;
}
type DeviceNames = Record<string, string>; // key: long_address

const props = defineProps<{
  devices: DaliDevice[],
  deviceNames: DeviceNames
}>();

const selectedScene = ref(0);
const sceneLevels = ref<Record<string, number>>({}); // key: long_address
const actionInProgress = ref(false);
const message = ref('');
const isError = ref(false);

const sceneOptions = Array.from({ length: 16 }, (_, i) => i);

const getDeviceDisplayName = (device: DaliDevice) => {
  return props.deviceNames[device.long_address] || `Device ${device.short_address} (${device.long_address})`;
};

const initSceneLevels = () => {
  const levels: Record<string, number> = {};
  props.devices.forEach(device => {
    levels[device.long_address] = 254;
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
    setTimeout(() => { message.value = ''}, 3000);
  } catch (e) {
    message.value = 'Failed to save scene.';
    isError.value = true;
  } finally {
    actionInProgress.value = false;
  }
};

const setAll = (level: number) => {
  props.devices.forEach(device => {
    sceneLevels.value[device.long_address] = level;
  });
};

onMounted(initSceneLevels);
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
        <div v-for="device in devices" :key="device.long_address" class="device-item">
          <label :for="`level-${device.long_address}`">{{ getDeviceDisplayName(device) }}</label>
          <input type="range" min="0" max="254" step="1" :id="`level-${device.long_address}`" v-model.number="sceneLevels[device.long_address]">
          <span>{{ sceneLevels[device.long_address] }}</span>
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
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
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
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.device-item input[type="range"] {
  margin-bottom: 0;
}
</style>