<!--
  - Copyright (c) 2026 Alice-Trade Inc.
  - SPDX-License-Identifier: GPL-3.0-or-later
  -->

<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
import { api } from '../api';

interface DaliDevice {
  long_address: string;
  short_address: number;
  type: 'gear' | 'input';
  available: boolean;
  gtin?: string;
  level?: number;
  lamp_failure?: boolean;
  dt?: number | null;
  min?: number;
  max?: number;
  on_level?: number;
  fail_level?: number;
  supports_tc?: boolean;
  supports_rgb?: boolean;
}

const devices = ref<DaliDevice[]>([]);
const loading = ref(true);
const currentTab = ref<'gear' | 'input'>('gear');
const activeActionAddr = ref<string | null>(null);

const gears = computed(() => devices.value.filter(d => d.type === 'gear'));
const inputs = computed(() => devices.value.filter(d => d.type === 'input'));

const getDeviceTypeStr = (d: DaliDevice) => {
  if (d.type === 'input') return 'Sensor / Input';
  if (d.dt === 6) return 'LED (DT6)';
  if (d.dt === 8) {
    const modes: string[] = [];
    if (d.supports_tc) modes.push('CCT');
    if (d.supports_rgb) modes.push('RGB');
    return `Color DT8 ${modes.length ? '(' + modes.join('/') + ')' : ''}`;
  }
  return d.dt !== null && d.dt !== undefined ? `Type ${d.dt}` : 'Standard';
};

const loadData = async () => {
  loading.value = true;
  try {
    const res = await api.getDaliDevices();
    devices.value = (res.data as DaliDevice[]).sort((a, b) => a.short_address - b.short_address);
  } catch (e) {
    console.error('Failed to load devices', e);
  } finally {
    loading.value = false;
  }
};

const handlePowerToggle = async (d: DaliDevice, on: boolean) => {
  activeActionAddr.value = d.long_address;
  try {
    await api.controlDevice(d.long_address, undefined, on ? 'ON' : 'OFF');
    d.level = on ? (d.max || 254) : 0;
  } catch (e) {
    console.error('Control failed', e);
  } finally {
    activeActionAddr.value = null;
  }
};

const handleLevelChange = async (d: DaliDevice, newLevel: number) => {
  d.level = newLevel;
  activeActionAddr.value = d.long_address;
  try {
    await api.controlDevice(d.long_address, newLevel);
  } catch (e) {
    console.error('Level control failed', e);
  } finally {
    activeActionAddr.value = null;
  }
};

onMounted(loadData);
</script>

<template>
  <article :aria-busy="loading">
    <div class="header-with-button">
      <h3>DALI Devices</h3>
      <button @click="loadData" :disabled="loading" class="outline secondary icon-btn" title="Refresh">
        ⟳
      </button>
    </div>

    <div class="tabs">
      <button class="tab-button" :class="{ active: currentTab === 'gear' }" @click="currentTab = 'gear'">
        Luminaires ({{ gears.length }})
      </button>
      <button class="tab-button" :class="{ active: currentTab === 'input' }" @click="currentTab = 'input'">
        Input Devices ({{ inputs.length }})
      </button>
    </div>

    <div v-if="currentTab === 'gear'" class="table-container">
      <table class="striped" v-if="gears.length > 0">
        <thead>
        <tr>
          <th style="width: 15%;">Address</th>
          <th style="width: 25%;">Type / GTIN</th>
          <th style="width: 15%;">Status</th>
          <th style="width: 30%;">Live Control</th>
          <th style="width: 15%;">Parameters</th>
        </tr>
        </thead>
        <tbody>
        <tr v-for="d in gears" :key="d.long_address">
          <td>
            <strong>A{{ d.short_address }}</strong><br />
            <small class="addr-sub">{{ d.long_address }}</small>
          </td>
          <td>
            <span>{{ getDeviceTypeStr(d) }}</span><br />
            <small v-if="d.gtin" class="gtin-text">GTIN: {{ d.gtin }}</small>
          </td>
          <td>
              <span :class="{
                'badge-on': d.available && (d.level || 0) > 0 && !d.lamp_failure,
                'badge-off': d.available && (d.level || 0) === 0 && !d.lamp_failure,
                'badge-fail': !d.available || d.lamp_failure
              }">
                {{ !d.available ? 'Offline' : d.lamp_failure ? 'FAULT' : (d.level || 0) > 0 ? 'ON' : 'OFF' }}
              </span>
          </td>
          <td>
            <div class="control-box">
              <button class="outline" :class="{ secondary: (d.level || 0) === 0 }"
                      :disabled="!d.available || activeActionAddr === d.long_address"
                      @click="handlePowerToggle(d, (d.level || 0) === 0)">
                {{ (d.level || 0) > 0 ? 'OFF' : 'ON' }}
              </button>
              <input type="range" min="0" max="254" step="1"
                     :value="d.level || 0"
                     :disabled="!d.available || activeActionAddr === d.long_address"
                     @change="handleLevelChange(d, Number(($event.target as HTMLInputElement).value))" />
              <span class="level-indicator">{{ d.level || 0 }}</span>
            </div>
          </td>
          <td>
            <small v-if="d.min !== undefined">
              Min: {{ d.min }} / Max: {{ d.max }}<br />
              Fail: {{ d.fail_level }}
            </small>
            <small v-else class="muted">Syncing...</small>
          </td>
        </tr>
        </tbody>
      </table>
      <div v-else class="empty-msg">No luminaires detected on the bus.</div>
    </div>

    <div v-if="currentTab === 'input'" class="table-container">
      <table class="striped" v-if="inputs.length > 0">
        <thead>
        <tr>
          <th>Address</th>
          <th>Identifier / GTIN</th>
          <th>Status</th>
        </tr>
        </thead>
        <tbody>
        <tr v-for="d in inputs" :key="d.long_address">
          <td><strong>Input A{{ d.short_address }}</strong></td>
          <td>
            <small class="addr-sub">{{ d.long_address }}</small><br />
            <small v-if="d.gtin" class="gtin-text">GTIN: {{ d.gtin }}</small>
          </td>
          <td>
              <span :class="{ 'badge-input-ready': d.available, 'badge-fail': !d.available }">
                {{ d.available ? 'Online (Ready)' : 'Offline' }}
              </span>
          </td>
        </tr>
        </tbody>
      </table>
      <div v-else class="empty-msg">No control devices detected on the bus.</div>
    </div>
  </article>
</template>

<style scoped>
.header-with-button {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1rem;
}
.icon-btn {
  padding: 0.25rem 0.6rem;
  font-size: 1.1rem;
  width: auto;
  margin: 0;
}
.tabs {
  display: flex;
  gap: 1rem;
  margin-bottom: 1rem;
  border-bottom: 1px solid var(--pico-muted-border-color);
}
.tab-button {
  background: none;
  border: none;
  border-bottom: 3px solid transparent;
  padding: 0.5rem 1rem;
  cursor: pointer;
  color: var(--pico-muted-color);
  font-weight: bold;
  border-radius: 0;
}
.tab-button.active {
  border-bottom-color: var(--pico-primary);
  color: var(--pico-primary);
}
.table-container {
  overflow-x: auto;
}
.addr-sub {
  font-family: monospace;
  color: var(--pico-muted-color);
}
.gtin-text {
  font-family: monospace;
  font-size: 0.75em;
  color: var(--pico-primary);
}
.control-box {
  display: flex;
  gap: 0.5rem;
  align-items: center;
}
.control-box button {
  width: auto;
  padding: 0.25rem 0.6rem;
  margin: 0;
  font-size: 0.8rem;
}
.control-box input[type="range"] {
  margin: 0;
  flex: 1;
}
.level-indicator {
  font-family: monospace;
  font-size: 0.85em;
  min-width: 2.2rem;
  text-align: right;
}
.badge-on { color: var(--pico-color-green-500); font-weight: bold; }
.badge-off { color: var(--pico-muted-color); }
.badge-fail { color: var(--pico-color-red-500); font-weight: bold; }
.badge-input-ready { color: var(--pico-color-azure-500); font-weight: bold; }
.empty-msg { text-align: center; padding: 2rem; color: var(--pico-muted-color); }
.muted { font-style: italic; }
</style>
