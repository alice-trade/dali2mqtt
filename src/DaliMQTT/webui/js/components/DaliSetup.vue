<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
import { api } from '../api';
import DaliSceneEditor from './DaliSceneEditor.vue';

// Типы данных
interface DaliDevice {
  long_address: string;
  short_address: number;
  is_input_device?: boolean;
}

type GroupAssignments = Record<string, number[]>; // key: long_address
type GroupMatrix = Record<string, boolean[]>;    // key: long_address
type DeviceNames = Record<string, string>;       // key: long_address

// Состояние компонента
const devices = ref<DaliDevice[]>([]);
const deviceNames = ref<DeviceNames>({});
const groupMatrix = ref<GroupMatrix>({});

const pristineDeviceNames = ref<DeviceNames>({});
const pristineGroupMatrix = ref<GroupMatrix>({});

const loading = ref(true);
const actionInProgress = ref('');
const message = ref('');
const isError = ref(false);
const viewMode = ref<'management' | 'scenes'>('management');

const createGroupMatrix = (devices: DaliDevice[], assignments: GroupAssignments): GroupMatrix => {
  const matrix: GroupMatrix = {};
  devices.forEach((device) => {
    const groupList = assignments[device.long_address] || [];
    const boolArray = Array(16).fill(false);
    groupList.forEach(g => {
      if (g >= 0 && g < 16) boolArray[g] = true;
    });
    matrix[device.long_address] = boolArray;
  });
  return matrix;
};

const isDirty = computed(() => {
  return JSON.stringify(deviceNames.value) !== JSON.stringify(pristineDeviceNames.value) ||
      JSON.stringify(groupMatrix.value) !== JSON.stringify(pristineGroupMatrix.value);
});

const loadData = async () => {
  loading.value = true;
  message.value = '';
  isError.value = false;
  try {
    const [devicesRes, namesRes, groupsRes] = await Promise.all([
      api.getDaliDevices(),
      api.getDaliNames(),
      api.getDaliGroups(),
    ]);

    const sortedDevices: DaliDevice[] = devicesRes.data.sort((a: DaliDevice, b: DaliDevice) => a.short_address - b.short_address);
    devices.value = sortedDevices;

    const names: DeviceNames = namesRes.data;
    const groups: GroupAssignments = groupsRes.data;

    sortedDevices.forEach((device: DaliDevice) => {
      if (!names[device.long_address]) names[device.long_address] = "";
      if (!groups[device.long_address]) groups[device.long_address] = [];
    });

    const matrix = createGroupMatrix(sortedDevices, groups);

    const namesClone = JSON.parse(JSON.stringify(names));
    const matrixClone = JSON.parse(JSON.stringify(matrix));

    deviceNames.value = namesClone;
    pristineDeviceNames.value = JSON.parse(JSON.stringify(namesClone));
    groupMatrix.value = matrixClone;
    pristineGroupMatrix.value = JSON.parse(JSON.stringify(matrixClone));

  } catch (e) {
    message.value = 'Не удалось загрузить данные DALI. Проверьте соединение с устройством.';
    isError.value = true;
  } finally {
    loading.value = false;
  }
};

const pollStatus = (action: 'scan' | 'init' | 'refresh', successMessage: string) => {
  const intervalId = setInterval(async () => {
    try {
      const res = await api.getDaliStatus();
      if (res.data.status === 'idle') {
        clearInterval(intervalId);
        message.value = successMessage;
        await loadData();
        actionInProgress.value = '';
        setTimeout(() => { if (message.value === successMessage) message.value = ''; }, 3000);
      } else {
        let statusText = res.data.status;
        if (statusText === 'scanning') statusText = 'сканирование';
        else if (statusText === 'initializing') statusText = 'инициализация';
        else if (statusText === 'refreshing_groups') statusText = 'обновление групп';
        message.value = `Выполняется: ${statusText}...`;
      }
    } catch (e) {
      clearInterval(intervalId);
      message.value = `Ошибка при проверке статуса: ${action}.`;
      isError.value = true;
      actionInProgress.value = '';
    }
  }, 2000);
};

const runAction = async (action: 'scan' | 'init' | 'save' | 'refresh', asyncFn: () => Promise<any>, successMessage: string, isAsyncDali: boolean = false) => {
  actionInProgress.value = action;
  message.value = `Выполняется: ${action}...`;
  isError.value = false;
  try {
    await asyncFn();
  } catch (e) {
    message.value = `Произошла ошибка во время выполнения: ${action}.`;
    isError.value = true;
  } finally {
    if (!isError.value) {
      if(isAsyncDali) {
        actionInProgress.value = action;
        pollStatus(action as 'scan' | 'init' | 'refresh', successMessage);
      } else {
        message.value = successMessage;
        actionInProgress.value = '';
        setTimeout(() => {
          if (message.value === successMessage) message.value = '';
        }, 3000);
      }
    } else {
      actionInProgress.value = '';
    }
  }
};

const handleScan = () => {
  runAction('scan', api.daliScan, 'Сканирование завершено!', true);
};

const handleInitialize = () => {
  if (!confirm('Это действие назначит новые короткие адреса неинициализированным устройствам на шине. Это необратимо. Вы уверены?')) {
    return;
  }
  runAction('init', api.daliInitialize, 'Инициализация завершена!', true);
};

const handleRefreshGroups = () => {
  runAction('refresh', api.daliRefreshGroups, 'Состояния групп успешно обновлены!', true);
};

const handleSaveChanges = () => {
  const savePromises: Promise<any>[] = [];

  if (JSON.stringify(deviceNames.value) !== JSON.stringify(pristineDeviceNames.value)) {
    savePromises.push(api.saveDaliNames(deviceNames.value));
  }

  if (JSON.stringify(groupMatrix.value) !== JSON.stringify(pristineGroupMatrix.value)) {
    const payload: GroupAssignments = {};
    for(const long_addr in groupMatrix.value) {
      const groups: number[] = [];
      groupMatrix.value[long_addr].forEach((isMember, index) => {
        if (isMember) groups.push(index);
      });
      payload[long_addr] = groups;
    }
    savePromises.push(api.saveDaliGroups(payload));
  }

  if (savePromises.length === 0) return;

  runAction('save', () => Promise.all(savePromises), 'Изменения успешно сохранены!').then(() => {
    if (!isError.value) {
      pristineDeviceNames.value = JSON.parse(JSON.stringify(deviceNames.value));
      pristineGroupMatrix.value = JSON.parse(JSON.stringify(groupMatrix.value));
    }
  });
};

const handleDiscardChanges = () => {
  deviceNames.value = JSON.parse(JSON.stringify(pristineDeviceNames.value));
  groupMatrix.value = JSON.parse(JSON.stringify(pristineGroupMatrix.value));
};

onMounted(loadData);
</script>

<template>
  <article :aria-busy="loading || !!actionInProgress">
    <header>
      <h3>Управление шиной DALI</h3>
      <p>Обнаружение устройств, назначение групп и настройка сцен.</p>
    </header>

    <div class="grid">
      <button @click="handleScan" :disabled="!!actionInProgress" :aria-busy="actionInProgress === 'scan'">
        Сканировать шину
      </button>
      <button @click="handleInitialize" :disabled="!!actionInProgress" :aria-busy="actionInProgress === 'init'" class="contrast">
        Инициализировать новые устройства
      </button>
    </div>

    <p v-if="message" :style="{ color: isError ? 'var(--pico-color-red-500)' : 'var(--pico-color-green-500)' }">{{ message }}</p>

    <div v-if="!loading">
      <nav>
        <ul>
          <li><a href="#" :class="{ 'secondary': viewMode !== 'management' }" @click.prevent="viewMode = 'management'">Управление группами</a></li>
          <li><a href="#" :class="{ 'secondary': viewMode !== 'scenes' }" @click.prevent="viewMode = 'scenes'">Редактор сцен</a></li>
        </ul>
      </nav>

      <div v-if="devices.length === 0" class="empty-state">
        <p><strong>Устройства на шине DALI не найдены.</strong></p>
        <p>Попробуйте просканировать шину или инициализировать новые балласты, если они подключены.</p>
      </div>

      <div v-if="viewMode === 'management' && devices.length > 0">
        <div class="save-bar" v-if="isDirty">
          <span>У вас есть несохраненные изменения.</span>
          <div class="grid">
            <button class="secondary outline" @click="handleDiscardChanges" :disabled="actionInProgress === 'save'">Отменить</button>
            <button @click="handleSaveChanges" :aria-busy="actionInProgress === 'save'">Сохранить изменения</button>
          </div>
        </div>

        <div class="management-header">
          <h4>Найдено устройств: ({{ devices.length }}/64)</h4>
          <button @click="handleRefreshGroups" :disabled="!!actionInProgress" :aria-busy="actionInProgress === 'refresh'" class="secondary outline">
                      Обновить статус групп
          </button>
        </div>
        <div class="devices-grid">
          <div v-for="device in devices" :key="device.long_address" class="device-card">
            <header class="card-header">
              <div style="display: flex; justify-content: space-between; align-items: center;">
                <div>
                  <strong>Устройство {{ device.short_address }}</strong>
                  <small class="long-address-text">{{ device.long_address }}</small>
                </div>
                <div v-if="device.is_input_device">
                  <span style="font-size: 0.7em; background-color: var(--pico-primary-background); color: var(--pico-primary); padding: 2px 6px; border-radius: 4px;">Input Device</span>
                </div>
              </div>
            </header>
            <div class="card-body">
              <label :for="`name-${device.long_address}`">Имя устройства</label>
              <input type="text" :id="`name-${device.long_address}`" v-model="deviceNames[device.long_address]" placeholder="например, Свет в офисе 1" />

              <label>Членство в группах</label>
              <div class="group-chips">
                <template v-for="i in 16" :key="i">
                  <label :for="`check-${device.long_address}-${i-1}`" class="chip" :class="{ 'active': groupMatrix[device.long_address] && groupMatrix[device.long_address][i-1] }">
                    <input type="checkbox" role="switch" :id="`check-${device.long_address}-${i-1}`" v-model="groupMatrix[device.long_address][i-1]" />
                    G{{ i-1 }}
                  </label>
                </template>
              </div>
            </div>
          </div>
        </div>
      </div>

      <DaliSceneEditor v-if="viewMode === 'scenes' && devices.length > 0" :devices="devices" :device-names="deviceNames" />
    </div>
  </article>
</template>

<style scoped>
.long-address-text {
  color: var(--pico-muted-color);
  font-family: monospace;
  font-size: 0.8em;
  display: block;
  margin-top: 0.2rem;
}
.empty-state {
  text-align: center;
  padding: 2rem;
  border: 2px dashed var(--pico-muted-border-color);
  border-radius: var(--pico-border-radius);
  margin-top: 1rem;
}

.save-bar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 1rem;
  background-color: var(--pico-card-background-color);
  border: 1px solid var(--pico-card-border-color);
  border-radius: var(--pico-border-radius);
  margin-bottom: 1.5rem;
  position: sticky;
  top: 1rem;
  z-index: 10;
  box-shadow: var(--pico-box-shadow);
}
.save-bar .grid {
  margin-bottom: 0;
  gap: 0.5rem;
}

.devices-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
  gap: 1.5rem;
  margin-top: 1rem;
}

.device-card {
  background-color: var(--pico-card-background-color);
  border: 1px solid var(--pico-card-border-color);
  border-radius: var(--pico-border-radius);
  box-shadow: var(--pico-card-box-shadow);
  display: flex;
  flex-direction: column;
}

.card-header {
  padding: 1rem 1.25rem;
  border-bottom: 1px solid var(--pico-card-border-color);
  background-color: var(--pico-table-header-background);
}
.card-header strong {
  font-size: 1.1em;
}

.card-body {
  padding: 1.25rem;
  flex-grow: 1;
}
.card-body label {
  margin-top: 0.75rem;
  margin-bottom: 0.25rem;
  font-weight: bold;
  color: var(--pico-secondary);
  font-size: 0.9em;
}
.card-body input[type="text"] {
  margin-bottom: 0.5rem;
}

.group-chips {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 0.5rem;
  margin-top: 0.5rem;
}

.chip {
  display: inline-flex;
  justify-content: center;
  align-items: center;
  padding: 0.375rem 0.5rem;
  border-radius: var(--pico-border-radius);
  background-color: var(--pico-muted-background-color);
  color: var(--pico-muted-color);
  font-size: 0.85em;
  font-weight: 600;
  text-align: center;
  cursor: pointer;
  transition: background-color 0.2s ease, color 0.2s ease, border-color 0.2s ease;
  border: 1px solid var(--pico-muted-border-color);
  user-select: none;
}
.chip:hover {
  background-color: var(--pico-secondary-hover);
  border-color: var(--pico-secondary-hover);
  color: var(--pico-secondary-inverse);
}

.chip.active {
  background-color: var(--pico-primary);
  border-color: var(--pico-primary);
  color: var(--pico-primary-inverse);
}
.chip.active:hover {
  background-color: var(--pico-primary-hover);
  border-color: var(--pico-primary-hover);
}

.chip input[type="checkbox"] {
  display: none;
}
</style>