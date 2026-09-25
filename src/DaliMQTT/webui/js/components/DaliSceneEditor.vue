<!--
  - Copyright (c) 2026 Alice-Trade Inc.
  - SPDX-License-Identifier: GPL-3.0-or-later
  -->

<script setup lang="ts">
import { ref, onMounted, watch } from 'vue';
import { api } from '../api';

interface DaliDevice {
  long_address: string;
  short_address: number;
}
type DeviceNames = Record<string, string>;

const props = defineProps<{
  devices: DaliDevice[];
  deviceNames: DeviceNames;
}>();

const selectedScene = ref(0);
const sceneLevels = ref<Record<string, number>>({});
const actionInProgress = ref(false);
const message = ref('');
const isError = ref(false);

const sceneOptions = Array.from({ length: 16 }, (_, i) => i);

const getDeviceDisplayName = (device: DaliDevice) => {
  return props.deviceNames[device.long_address] || `Luminaire A${device.short_address}`;
};

const loadSceneData = async () => {
  actionInProgress.value = true;
  message.value = '';
  isError.value = false;
  try {
    const levels: Record<string, number> = {};
    props.devices.forEach(device => {
      levels[device.long_address] = 255;
    });

    const res = await api.getDaliScene(selectedScene.value);
    if (res.data && res.data.levels) {
      for (const [longAddr, val] of Object.entries(res.data.levels)) {
        levels[longAddr] = Number(val);
      }
    }
    sceneLevels.value = levels;
  } catch {
    message.value = 'Failed to load current scene configuration.';
    isError.value = true;
  } finally {
    actionInProgress.value = false;
  }
};

watch(selectedScene, loadSceneData);
onMounted(loadSceneData);

const handleSaveScene = async () => {
  actionInProgress.value = true;
  message.value = `Saving Scene ${selectedScene.value} to ballasts...`;
  isError.value = false;

  const payload = {
    scene_id: selectedScene.value,
    levels: sceneLevels.value
  };

  try {
    await api.saveDaliScene(payload);
    message.value = `Scene ${selectedScene.value} saved successfully!`;
    setTimeout(() => { if (message.value.includes('saved')) message.value = ''; }, 3000);
  } catch {
    message.value = 'Failed to save scene.';
    isError.value = true;
  } finally {
    actionInProgress.value = false;
  }
};

const setAllLevels = (level: number) => {
  props.devices.forEach(device => {
    sceneLevels.value[device.long_address] = level;
  });
};

const toggleInScene = (longAddr: string, include: boolean) => {
  sceneLevels.value[longAddr] = include ? 254 : 255;
};
</script>

<template>
  <div class="scene-editor">
    <div class="management-header">
      <div>
        <h4>Scene Editor</h4>
        <p>Configure brightness levels for each device and save them to a scene. The settings are stored directly on the DALI ballasts.</p>      </div>
    </div>

    <form @submit.prevent="handleSaveScene">
      <div class="grid controls-bar">
        <div>
          <label for="scene-select">Select Scene</label>
          <select id="scene-select" v-model="selectedScene" :disabled="actionInProgress">
            <option v-for="scene in sceneOptions" :key="scene" :value="scene">Scene {{ scene }}</option>
          </select>
        </div>

        <div>
          <label>Quick Preset for All:</label>
          <div class="grid presets-grid">
            <button type="button" class="outline" @click="setAllLevels(254)">100%</button>
            <button type="button" class="outline" @click="setAllLevels(127)">50%</button>
            <button type="button" class="outline" @click="setAllLevels(0)">OFF</button>
            <button type="button" class="outline secondary" @click="setAllLevels(255)">Exclude All</button>
          </div>
        </div>
      </div>

      <div class="device-list">
        <div v-for="device in devices" :key="device.long_address" class="device-scene-card">
          <div class="device-info">
            <strong>{{ getDeviceDisplayName(device) }}</strong>
            <small class="addr-tag">{{ device.long_address }}</small>
          </div>

          <div class="slider-row">
            <label class="mask-toggle">
              <input type="checkbox" role="switch"
                     :checked="sceneLevels[device.long_address] !== 255"
                     @change="toggleInScene(device.long_address, ($event.target as HTMLInputElement).checked)" />
              <span class="mask-label">{{ sceneLevels[device.long_address] !== 255 ? 'In Scene' : 'Ignored (MASK)' }}</span>
            </label>

            <input type="range" min="0" max="254" step="1"
                   :disabled="sceneLevels[device.long_address] === 255"
                   :value="sceneLevels[device.long_address] === 255 ? 0 : sceneLevels[device.long_address]"
                   @input="sceneLevels[device.long_address] = Number(($event.target as HTMLInputElement).value)" />

            <span class="level-display" :class="{ 'text-muted': sceneLevels[device.long_address] === 255 }">
              {{ sceneLevels[device.long_address] === 255 ? 'MASK' : sceneLevels[device.long_address] }}
            </span>
          </div>
        </div>
      </div>

      <div class="submit-bar">
        <button type="submit" :disabled="actionInProgress" :aria-busy="actionInProgress" style="width: auto;">
          Save Scene {{ selectedScene }}
        </button>
        <span v-if="message" class="status-note" :class="{ 'err-note': isError }">{{ message }}</span>
      </div>
    </form>
  </div>
</template>

<style scoped>
.scene-editor {
  margin-top: 1rem;
}
.controls-bar {
  align-items: end;
  margin-bottom: 1.5rem;
}
.presets-grid {
  margin-bottom: 0;
  gap: 0.5rem;
}
.presets-grid button {
  padding: 0.4rem 0.5rem;
  font-size: 0.85rem;
  margin-bottom: 0;
}
.device-list {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 1.25rem;
  margin-bottom: 1.5rem;
}
.device-scene-card {
  padding: 1rem;
  border: 1px solid var(--pico-card-border-color);
  background-color: var(--pico-card-background-color);
  border-radius: var(--pico-border-radius);
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}
.device-info {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
}
.addr-tag {
  font-family: monospace;
  color: var(--pico-muted-color);
}
.slider-row {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}
.mask-toggle {
  display: flex;
  align-items: center;
  gap: 0.4rem;
  margin-bottom: 0;
  font-size: 0.8em;
  white-space: nowrap;
}
.mask-toggle input {
  margin-bottom: 0;
}
.slider-row input[type="range"] {
  margin-bottom: 0;
  flex: 1;
}
.level-display {
  font-family: monospace;
  font-size: 0.85em;
  min-width: 2.8rem;
  text-align: right;
  font-weight: bold;
}
.text-muted {
  color: var(--pico-muted-color);
  font-weight: normal;
}
.submit-bar {
  display: flex;
  align-items: center;
  gap: 1.5rem;
  margin-top: 1.5rem;
}
.status-note {
  font-weight: bold;
  color: var(--pico-color-green-500);
}
.err-note {
  color: var(--pico-color-red-500);
}
</style>
