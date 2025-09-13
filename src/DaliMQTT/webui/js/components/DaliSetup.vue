<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { api } from '../api';

type GroupAssignments = Record<string, number[]>; // { "0": [0, 15], "1": [1] }
type GroupMatrix = Record<string, boolean[]>;     // { "0": [true, false, ..., true], "1": [false, true, ...] }


const devices = ref<number[]>([]);
const deviceNames = ref<Record<string, string>>({});
const groupMatrix = ref<GroupMatrix>({});

const loading = ref(true);
const actionInProgress = ref('');
const message = ref('');
const isError = ref(false);
const viewMode = ref<'names' | 'groups'>('names');

const loadData = async () => {
  loading.value = true;
  message.value = '';
  try {
    const [devicesRes, namesRes, groupsRes] = await Promise.all([
      api.getDaliDevices(),
      api.getDaliNames(),
      api.getDaliGroups(),
    ]);
    devices.value = devicesRes.data.sort((a: number, b: number) => a - b);
    deviceNames.value = namesRes.data;

    const matrix: GroupMatrix = {};
    const apiGroups: GroupAssignments = groupsRes.data;
    devices.value.forEach(addr => {
      const groupList = apiGroups[addr] || [];
      const boolArray = Array(16).fill(false);
      groupList.forEach(g => boolArray[g] = true);
      matrix[addr] = boolArray;
    });
    groupMatrix.value = matrix;

  } catch (e) {
    message.value = 'Failed to load DALI data.';
    isError.value = true;
  } finally {
    loading.value = false;
  }
};

const handleScan = async () => {
  actionInProgress.value = 'scan';
  message.value = 'Scanning DALI bus... This may take a moment.';
  isError.value = false;
  try {
    await api.daliScan();
    message.value = 'Scan complete!';
    await loadData();
  } catch (e) {
    message.value = 'An error occurred during the scan.';
    isError.value = true;
  } finally {
    actionInProgress.value = '';
  }
};

const handleInitialize = async () => {
  if (!confirm('This will assign new short addresses to uncommissioned devices on the bus. Are you sure?')) {
    return;
  }
  actionInProgress.value = 'init';
  message.value = 'Initializing DALI devices... This can take up to a minute.';
  isError.value = false;
  try {
    await api.daliInitialize();
    message.value = 'Initialization complete!';
    await loadData();
  } catch (e) {
    message.value = 'An error occurred during initialization.';
    isError.value = true;
  } finally {
    actionInProgress.value = '';
  }
};

const handleSaveNames = async () => {
  actionInProgress.value = 'save_names';
  message.value = 'Saving names...';
  isError.value = false;
  try {
    await api.saveDaliNames(deviceNames.value);
    message.value = 'Device names saved successfully!';
  } catch (e) {
    message.value = 'Failed to save names.';
    isError.value = true;
  } finally {
    actionInProgress.value = '';
  }
};

const handleSaveGroups = async () => {
  actionInProgress.value = 'save_groups';
  message.value = 'Saving group assignments...';
  isError.value = false;
  try {
    const payload: GroupAssignments = {};
    for(const addr in groupMatrix.value) {
      const groups: number[] = [];
      groupMatrix.value[addr].forEach((isMember, index) => {
        if (isMember) groups.push(index);
      });
      payload[addr] = groups;
    }
    await api.saveDaliGroups(payload);
    message.value = 'Group assignments saved successfully!';
  } catch(e) {
    message.value = 'Failed to save group assignments.';
    isError.value = true;
  } finally {
    actionInProgress.value = '';
  }
}

const getDeviceDisplayName = (addr: number) => {
  return deviceNames.value[addr] || `Device ${addr}`;
};

onMounted(loadData);
</script>

<template>
  <article :aria-busy="loading || !!actionInProgress">
    <header>
      <h3>DALI Bus Control</h3>
      <p>Manage and discover devices on the DALI bus.</p>
    </header>

    <div class="grid">
      <button @click="handleScan" :disabled="!!actionInProgress" :aria-busy="actionInProgress === 'scan'">Scan Bus</button>
      <button @click="handleInitialize" :disabled="!!actionInProgress" :aria-busy="actionInProgress === 'init'" class="contrast">Initialize New Devices</button>
    </div>

    <p v-if="message" :style="{ color: isError ? 'var(--pico-color-red-500)' : 'var(--pico-color-green-500)' }">{{ message }}</p>

    <div v-if="!loading">
      <nav>
        <ul>
          <li><a href="#" :class="{ 'secondary': viewMode !== 'names' }" @click.prevent="viewMode = 'names'">Device Names</a></li>
          <li><a href="#" :class="{ 'secondary': viewMode !== 'groups' }" @click.prevent="viewMode = 'groups'">Group Assignments</a></li>
        </ul>
      </nav>

      <p v-if="devices.length === 0">No devices found on the DALI bus. Try scanning or initializing.</p>

      <!-- View for Device Names -->
      <div v-if="viewMode === 'names' && devices.length > 0">
        <h4>Discovered Devices ({{ devices.length }}/64)</h4>
        <form @submit.prevent="handleSaveNames">
          <div class="device-list">
            <div v-for="device in devices" :key="device" class="device-item">
              <label :for="`dali-name-${device}`">
                <strong>Addr. {{ device }}</strong>
              </label>
              <input
                  type="text"
                  :id="`dali-name-${device}`"
                  v-model="deviceNames[device]"
                  placeholder="Readable Name"
              />
            </div>
          </div>
          <button type="submit" :disabled="!!actionInProgress" :aria-busy="actionInProgress === 'save_names'">Save Names</button>
        </form>
      </div>

      <!-- View for Group Assignments -->
      <div v-if="viewMode === 'groups' && devices.length > 0">
        <h4>Group Assignments</h4>
        <form @submit.prevent="handleSaveGroups">
          <figure>
            <table class="striped">
              <thead>
              <tr>
                <th scope="col" style="min-width: 150px;">Device</th>
                <th scope="col" v-for="i in 16" :key="i" style="text-align: center;">G{{ i-1 }}</th>
              </tr>
              </thead>
              <tbody>
              <tr v-for="addr in devices" :key="addr">
                <th scope="row">{{ getDeviceDisplayName(addr) }}</th>
                <td v-for="i in 16" :key="i" style="text-align: center;">
                  <input type="checkbox" v-model="groupMatrix[addr][i-1]">
                </td>
              </tr>
              </tbody>
            </table>
          </figure>
          <button type="submit" :disabled="!!actionInProgress" :aria-busy="actionInProgress === 'save_groups'">Save Groups</button>
        </form>
      </div>
    </div>
  </article>
</template>

<style scoped>
.device-list {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
  gap: 1rem;
  margin-block: 1rem;
}
.device-item {
  display: flex;
  flex-direction: column;
}
.device-item label {
  margin-bottom: 0.25rem;
}
table input[type="checkbox"] {
  width: 1.25rem;
  height: 1.25rem;
}
</style>