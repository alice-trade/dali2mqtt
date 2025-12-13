<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { DaliDevice } from 'dalimqx';

const props = defineProps<{
  device: DaliDevice
}>();

const isOn = ref(false);
const brightness = ref(0);
const isOnline = ref(false);
const localStatusByte = ref<number | undefined>(0);

const syncFromDevice = () => {
  const s = props.device.state;
  isOn.value = s.state === 'ON';
  brightness.value = s.brightness || 0;
  isOnline.value = s.available ?? false;

  localStatusByte.value = s.status_byte;
};

const toggle = async () => {
  isOn.value = !isOn.value;
  if (isOn.value) await props.device.turnOn();
  else await props.device.turnOff();
};

const setBrightness = async (e: Event) => {
  const val = parseInt((e.target as HTMLInputElement).value);
  brightness.value = val;
  await props.device.setBrightness(val);
};

const statusFlags = computed(() => {
  if (localStatusByte.value === undefined) return [];

  const d = props.device;
  const flags = [];

  if (d.isLampFailure) flags.push({ label: 'Lamp Fail', color: 'bg-red-100 text-red-700 border-red-200' });
  if (d.isControlGearFailure) flags.push({ label: 'Gear Fail', color: 'bg-red-100 text-red-700 border-red-200' });
  if (d.isPowerFailure) flags.push({ label: 'Power Fail', color: 'bg-orange-100 text-orange-700 border-orange-200' });
  if (d.isLimitError) flags.push({ label: 'Limit Err', color: 'bg-yellow-100 text-yellow-700 border-yellow-200' });
  if (d.isFadeRunning) flags.push({ label: 'Fading', color: 'bg-blue-100 text-blue-700 border-blue-200' });
  if (d.isResetState) flags.push({ label: 'Reset', color: 'bg-gray-100 text-gray-700 border-gray-200' });

  return flags;
});

onMounted(() => {
  syncFromDevice();
  props.device.on('change', syncFromDevice);
});

onUnmounted(() => {
  props.device.off('change', syncFromDevice);
});
</script>

<template>
  <div
      class="relative p-5 rounded-xl border transition-all duration-300 flex flex-col gap-4"
      :class="[
      isOnline ? 'bg-white border-gray-200 shadow-sm' : 'bg-gray-50 border-gray-200 opacity-60 grayscale'
    ]"
  >
    <div class="flex justify-between items-start">
      <div>
        <div class="flex items-center gap-2">
          <div class="w-8 h-8 rounded-full flex items-center justify-center" :class="isOn ? 'bg-yellow-100 text-yellow-600' : 'bg-gray-100 text-gray-400'">
            <svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M15 14c.2-1 .7-1.7 1.5-2.5 1-1 1.5-2 1.5-3.5 0-3.33-2.69-6-6-6s-6 2.67-6 6c0 1.5.5 2.5 1.5 3.5.8.8 1.3 1.5 1.5 2.5"/><path d="M9 18h6"/><path d="M10 22h4"/></svg>
          </div>
          <div>
            <h3 class="font-bold text-gray-800 text-sm leading-tight">DALI Device</h3>
            <p class="text-[10px] text-gray-400 font-mono">{{ device.longAddress }}</p>
          </div>
        </div>
      </div>

      <button
          @click="toggle"
          :disabled="!isOnline"
          class="relative inline-flex h-6 w-11 items-center rounded-full transition-colors focus:outline-none focus:ring-2 focus:ring-blue-500 focus:ring-offset-2"
          :class="isOn ? 'bg-blue-600' : 'bg-gray-200'"
      >
        <span
            class="inline-block h-4 w-4 transform rounded-full bg-white transition-transform"
            :class="isOn ? 'translate-x-6' : 'translate-x-1'"
        />
      </button>
    </div>

    <div class="flex flex-wrap gap-1 min-h-[20px]">
      <span
          v-for="flag in statusFlags"
          :key="flag.label"
          class="px-1.5 py-0.5 rounded text-[10px] font-medium border"
          :class="flag.color"
      >
        {{ flag.label }}
      </span>
      <span v-if="statusFlags.length === 0 && isOnline" class="text-[10px] text-green-600 px-1.5 py-0.5 bg-green-50 border border-green-100 rounded">OK</span>
      <span v-if="!isOnline" class="text-[10px] text-gray-500">Offline</span>
    </div>

    <div class="space-y-2 pt-2 border-t border-gray-100">
      <div class="flex justify-between items-end text-xs text-gray-500 mb-1">
        <span>Brightness</span>
        <span class="font-mono font-medium text-gray-700">{{ brightness }} / 254</span>
      </div>
      <input
          type="range"
          min="0"
          max="254"
          :value="brightness"
          @change="setBrightness"
          :disabled="!isOnline"
          class="w-full h-2 bg-gray-200 rounded-lg appearance-none cursor-pointer accent-blue-600 disabled:cursor-not-allowed"
      />
    </div>
  </div>
</template>