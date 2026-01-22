<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
import { api } from '../api';

interface DaliDevice {
  long_address: string;
  short_address: number;
  type: 'gear' | 'input';
  available: boolean;
  gtin?: string;

  // Gear Specific
  level?: number;
  lamp_failure?: boolean;
  dt?: number | null;
  min?: number;
  max?: number;
  on_level?: number;
  fail_level?: number;
}

const devices = ref<DaliDevice[]>([]);
const loading = ref(true);
const currentTab = ref<'gear' | 'input'>('gear');

const getDeviceState = (d: DaliDevice) => {
  if (!d.available) return 'Offline';
  if (d.type === 'input') return 'Ready';
  return (d.level !== undefined && d.level > 0) ? `ON (${Math.round((d.level / 254) * 100)}%)` : 'OFF';
};

const getDeviceTypeStr = (d: DaliDevice) => {
  if (d.type === 'input') return 'Input Device';
  const dt = d.dt;
  if (dt === null || dt === undefined) return 'Standard';
  if (dt === 6) return 'LED (Type 6)';
  if (dt === 8) return 'RGB/W (Type 8)';
  return `Type ${dt}`;
};

const loadData = async () => {
  loading.value = true;
  try {
    const devicesRes = await api.getDaliDevices();
    devices.value = (devicesRes.data as DaliDevice[]).sort((a, b) => a.short_address - b.short_address);
  } catch (e) {
    console.error(e);
  } finally {
    loading.value = false;
  }
};

const gears = computed(() => devices.value.filter(d => d.type === 'gear'));
const inputs = computed(() => devices.value.filter(d => d.type === 'input'));

onMounted(loadData);
</script>

<template>
  <article :aria-busy="loading">
    <div class="header-with-button">
      <h3>DALI Devices</h3>
      <button @click="loadData" :disabled="loading" class="outline secondary icon-button" title="Refresh">
        ⟳
      </button>
    </div>

    <!-- Tabs -->
    <div class="tabs">
      <button
          class="tab-button"
          :class="{ active: currentTab === 'gear' }"
          @click="currentTab = 'gear'">
        Luminaires ({{ gears.length }})
      </button>
      <button
          class="tab-button"
          :class="{ active: currentTab === 'input' }"
          @click="currentTab = 'input'">
        Input Devices ({{ inputs.length }})
      </button>
    </div>

    <!-- Gear Table -->
    <div v-if="currentTab === 'gear'" class="table-container">
      <table class="striped" v-if="gears.length > 0">
        <thead>
        <tr>
          <th>Addr</th>
          <th>Type</th>
          <th>State</th>
          <th>Level</th>
          <th>Details</th>
        </tr>
        </thead>
        <tbody>
        <tr v-for="d in gears" :key="d.long_address">
          <td>
            <strong>{{ d.short_address }}</strong>
            <br/>
            <small>{{ d.long_address }}</small>
            <br v-if="d.gtin"/>
            <small v-if="d.gtin" class="gtin-text">GTIN: {{ d.gtin }}</small>
          </td>
          <td>{{ getDeviceTypeStr(d) }}</td>
          <td>
              <span :class="{
                'badge-on': d.available && (d.level || 0) > 0 && !d.lamp_failure,
                'badge-off': d.available && (d.level || 0) === 0 && !d.lamp_failure,
                'badge-fail': d.lamp_failure || !d.available
              }">
                {{ d.lamp_failure ? 'FAILURE' : getDeviceState(d) }}
              </span>
          </td>
          <td>{{ d.level }}</td>
          <td>
            <small v-if="d.min !== undefined">
              Range: {{ d.min }}-{{ d.max }}<br/>
              PowerOn: {{ d.on_level }}<br/>
              Fail: {{ d.fail_level }}
            </small>
            <small v-else class="muted">Loading info...</small>
          </td>
        </tr>
        </tbody>
      </table>
      <div v-else class="empty-msg">No luminaires found.</div>
    </div>

    <!-- Input Devices Table -->
    <div v-if="currentTab === 'input'" class="table-container">
      <table class="striped" v-if="inputs.length > 0">
        <thead>
        <tr>
          <th>Addr</th>
          <th>Identity</th>
          <th>Status</th>
        </tr>
        </thead>
        <tbody>
        <tr v-for="d in inputs" :key="d.long_address">
          <td><strong>{{ d.short_address }}</strong></td>
          <td>
            <small>{{ d.long_address }}</small>
            <br v-if="d.gtin"/>
            <small v-if="d.gtin" class="gtin-text">GTIN: {{ d.gtin }}</small>
          </td>
          <td>
             <span :class="{ 'badge-input-ready': d.available, 'badge-fail': !d.available }">
               {{ d.available ? 'Online' : 'Offline' }}
             </span>
          </td>
        </tr>
        </tbody>
      </table>
      <div v-else class="empty-msg">No input devices found.</div>
    </div>

    <div class="footer-note">
      <small>Go to DALI Control to scan bus or re-address devices.</small>
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
.icon-button {
  padding: 0.25rem 0.75rem;
  font-size: 1.2rem;
  line-height: 1;
  width: auto;
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
small {
  font-family: monospace;
  color: var(--pico-muted-color);
}
.gtin-text {
  font-size: 0.75em;
  color: var(--pico-primary);
}
.badge-on { color: var(--pico-primary); font-weight: bold; }
.badge-off { color: var(--pico-muted-color); }
.badge-fail { color: var(--pico-color-red-500); font-weight: bold; }
.badge-input-ready { color: var(--pico-color-azure-500); font-weight: bold; }
.empty-msg { text-align: center; padding: 2rem; color: var(--pico-muted-color); }
.footer-note { margin-top: 1rem; text-align: right; }
.muted { font-style: italic; }
</style>
