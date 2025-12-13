<script setup lang="ts">
import { ref, computed } from 'vue';
import { useDali } from './composables/useDali';
import LightCard from './components/LightCard.vue';

const {
  isConnected,
  isConnecting,
  isScanning,
  connect,
  disconnect,
  scan,
  devices,
  errorMsg
} = useDali();

// Settings
const brokerHost = ref('localhost');
const brokerPort = ref(9001);
const baseTopic = ref('dali_mqtt');

const deviceList = computed(() => {
  return Array.from(devices.values()).sort((a, b) => {
    return a.longAddress.localeCompare(b.longAddress);
  });
});

const handleConnect = () => {
  connect(brokerHost.value, brokerPort.value, baseTopic.value);
};
</script>

<template>
  <div class="min-h-screen bg-gray-50 font-sans text-gray-900">

    <header class="bg-white border-b border-gray-200 sticky top-0 z-10">
      <div class="max-w-5xl mx-auto px-4 sm:px-6 h-16 flex items-center justify-between">
        <div class="flex items-center gap-2">
          <div class="w-8 h-8 bg-blue-600 text-white rounded-lg flex items-center justify-center font-bold">D</div>
          <h1 class="text-xl font-bold tracking-tight">DALIMQX Web Panel</h1>
        </div>

        <div class="flex items-center gap-4">
          <div v-if="isConnected" class="flex items-center gap-2 px-3 py-1 bg-green-50 text-green-700 rounded-full border border-green-100 text-sm">
            <span class="w-2 h-2 rounded-full bg-green-500 animate-pulse"></span>
            <span class="font-medium">Online</span>
          </div>

          <button
              v-if="isConnected"
              @click="disconnect"
              class="text-sm text-gray-500 hover:text-red-600 transition-colors font-medium"
          >
            Logout
          </button>
        </div>
      </div>
    </header>

    <main class="max-w-5xl mx-auto px-4 sm:px-6 py-8">

      <div v-if="errorMsg" class="mb-6 bg-red-50 border border-red-200 rounded-lg p-4 flex items-start gap-3">
        <svg xmlns="http://www.w3.org/2000/svg" class="h-5 w-5 text-red-500 mt-0.5" viewBox="0 0 20 20" fill="currentColor"><path fill-rule="evenodd" d="M18 10a8 8 0 11-16 0 8 8 0 0116 0zm-7 4a1 1 0 11-2 0 1 1 0 012 0zm-1-9a1 1 0 00-1 1v4a1 1 0 102 0V6a1 1 0 00-1-1z" clip-rule="evenodd" /></svg>
        <div>
          <h3 class="text-sm font-medium text-red-800">Connection Error</h3>
          <p class="text-sm text-red-700 mt-1">{{ errorMsg }}</p>
        </div>
        <button @click="errorMsg = null" class="ml-auto text-red-500 hover:text-red-800">✕</button>
      </div>

      <div v-if="!isConnected" class="max-w-md mx-auto mt-10">
        <div class="bg-white rounded-2xl shadow-xl border border-gray-100 overflow-hidden">
          <div class="p-8">
            <h2 class="text-2xl font-bold mb-2">Connect to Bridge</h2>
            <p class="text-gray-500 text-sm mb-6">Enter your MQTT broker Websocket details.</p>

            <div class="space-y-4">
              <div>
                <label class="block text-xs font-bold text-gray-500 uppercase tracking-wider mb-1">Broker Host</label>
                <input v-model="brokerHost" type="text" class="w-full p-3 bg-gray-50 border border-gray-200 rounded-lg focus:bg-white focus:ring-2 focus:ring-blue-500 focus:border-blue-500 outline-none transition-all" placeholder="localhost" />
              </div>

              <div class="grid grid-cols-2 gap-4">
                <div>
                  <label class="block text-xs font-bold text-gray-500 uppercase tracking-wider mb-1">WS Port</label>
                  <input v-model="brokerPort" type="number" class="w-full p-3 bg-gray-50 border border-gray-200 rounded-lg focus:bg-white focus:ring-2 focus:ring-blue-500 focus:border-blue-500 outline-none transition-all" placeholder="9001" />
                </div>
                <div>
                  <label class="block text-xs font-bold text-gray-500 uppercase tracking-wider mb-1">Base Topic</label>
                  <input v-model="baseTopic" type="text" class="w-full p-3 bg-gray-50 border border-gray-200 rounded-lg focus:bg-white focus:ring-2 focus:ring-blue-500 focus:border-blue-500 outline-none transition-all" placeholder="dali_mqtt" />
                </div>
              </div>

              <button
                  @click="handleConnect"
                  :disabled="isConnecting"
                  class="w-full mt-6 bg-gray-900 text-white py-3.5 rounded-xl hover:bg-black transition-all font-semibold shadow-lg hover:shadow-xl disabled:opacity-70 flex justify-center items-center gap-2"
              >
                <span v-if="isConnecting" class="w-4 h-4 border-2 border-white/30 border-t-white rounded-full animate-spin"></span>
                {{ isConnecting ? 'Connecting...' : 'Connect' }}
              </button>
            </div>
          </div>
        </div>
      </div>

      <div v-else>
        <div class="flex flex-col sm:flex-row sm:items-center justify-between gap-4 mb-6">
          <div>
            <h2 class="text-lg font-bold text-gray-900">Devices</h2>
            <p class="text-sm text-gray-500">Found {{ deviceList.length }} devices on bus</p>
          </div>

          <button
              @click="scan"
              :disabled="isScanning"
              class="flex items-center gap-2 px-4 py-2 bg-white border border-gray-200 rounded-lg text-sm font-medium text-gray-700 hover:bg-gray-50 hover:border-gray-300 transition-colors shadow-sm disabled:opacity-70 disabled:cursor-wait"
          >
            <svg v-if="!isScanning" xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 0 0-9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"/><path d="M3 3v5h5"/><path d="M3 12a9 9 0 0 0 9 9 9.75 9.75 0 0 0 6.74-2.74L21 16"/><path d="M16 21h5v-5"/></svg>
            <svg v-else class="animate-spin" xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-6.219-8.56"/></svg>
            {{ isScanning ? 'Scanning Bus...' : 'Scan Bus' }}
          </button>
        </div>

        <div v-if="deviceList.length > 0" class="grid grid-cols-1 sm:grid-cols-2 md:grid-cols-3 lg:grid-cols-4 gap-4">
          <LightCard
              v-for="device in deviceList"
              :key="device.longAddress"
              :device="device"
          />
        </div>

        <div v-else class="text-center py-20 bg-white rounded-xl border border-dashed border-gray-300">
          <div class="w-16 h-16 bg-gray-50 rounded-full flex items-center justify-center mx-auto mb-4 text-gray-300">
            <svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M10 10h.01"/><path d="M14 10h.01"/><path d="M10 14h.01"/><path d="M14 14h.01"/><path d="M7 7h10v10H7z"/><path d="M21 12a9 9 0 0 0-9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"/><path d="M3 3v5h5"/><path d="M3 12a9 9 0 0 0 9 9 9.75 9.75 0 0 0 6.74-2.74L21 16"/><path d="M16 21h5v-5"/></svg>
          </div>
          <h3 class="text-gray-900 font-medium">No devices found</h3>
          <p class="text-gray-500 text-sm mt-1 max-w-xs mx-auto">Click "Scan Bus" to detect devices connected to the DALI bridge.</p>
        </div>

      </div>
    </main>
  </div>
</template>