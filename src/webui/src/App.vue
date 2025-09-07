<script setup lang="ts">
import { ref } from 'vue';
import { api, setAuth } from './api';
import SettingsPage from './components/SettingsPage.vue';
import InfoPage from './components/InfoPage.vue';

// Состояния
const loggedIn = ref(false);
const username = ref('admin');
const password = ref('');
const error = ref('');
const currentView = ref('settings'); // 'settings' или 'info'

const handleLogin = async () => {
  error.value = '';
  if (!username.value || !password.value) {
    error.value = 'Please enter username and password.';
    return;
  }
  try {
    setAuth(username.value, password.value);
    await api.getInfo();
    loggedIn.value = true;
  } catch (e: any) {
    if (e.response && e.response.status === 401) {
      error.value = 'Invalid credentials. Please try again.';
    } else {
      error.value = 'Failed to connect to the device.';
    }
  }
};
</script>

<template>
  <main class="container">
    <div v-if="!loggedIn">
      <article>
        <h2 style="text-align: center;">DALI-MQTT Bridge Login</h2>
        <form @submit.prevent="handleLogin">
          <label for="username">Username</label>
          <input type="text" id="username" v-model="username" required>

          <label for="password">Password</label>
          <input type="password" id="password" v-model="password" required>

          <button type="submit">Login</button>
        </form>
        <p v-if="error" style="color: var(--pico-color-red-500);">{{ error }}</p>
      </article>
    </div>

    <div v-else>
      <header>
        <nav>
          <ul>
            <li><strong>DALI-MQTT Bridge</strong></li>
          </ul>
          <ul>
            <li><a href="#" :class="{ 'secondary': currentView !== 'settings' }" @click="currentView = 'settings'">Settings</a></li>
            <li><a href="#" :class="{ 'secondary': currentView !== 'info' }" @click="currentView = 'info'">Info</a></li>
          </ul>
        </nav>
      </header>

      <SettingsPage v-if="currentView === 'settings'" />
      <InfoPage v-if="currentView === 'info'" />
    </div>
  </main>
</template>