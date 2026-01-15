<script setup lang="ts">
  import { ref, onMounted } from 'vue';
  import { api, setAuth, setAuthToken, clearAuth } from './api';
  import SettingsPage from './components/SettingsPage.vue';
  import DaliSetup from './components/DaliSetup.vue';
  import StatusPage from './components/StatusPage.vue';
  import DevicesPage from './components/DevicesPage.vue';

  const loggedIn = ref(false);
  const username = ref('admin');
  const password = ref('');
  const error = ref('');
  const loading = ref(false);
  const currentView = ref('status');

  const handleLogin = async () => {
    error.value = '';
    if (!username.value || !password.value) {
      error.value = 'Please enter username and password.';
      return;
    }
    loading.value = true;
    try {
      const token = setAuth(username.value, password.value);
      await api.getInfo();
      localStorage.setItem('auth', token);
      loggedIn.value = true;
    } catch (e: any) {
      localStorage.removeItem('auth');
      if (e.response && e.response.status === 401) {
        error.value = 'Invalid credentials. Please try again.';
      } else {
        error.value = 'Failed to connect to the device.';
      }
    } finally {
      loading.value = false;
    }
  };

  const handleLogout = () => {
    clearAuth();
    localStorage.removeItem('auth');
    loggedIn.value = false;
    password.value = '';
  };


  const checkLogin = async () => {
    const token = localStorage.getItem('auth');
    if (token) {
      try {
        setAuthToken(token);
        await api.getInfo();
        loggedIn.value = true;
      } catch (e) {
        console.log("Session token expired or invalid");
        handleLogout();
      }
    }
  };

  onMounted(checkLogin);

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

          <button type="submit" :aria-busy="loading">Login</button>
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
            <li><a href="#" :class="{ 'secondary': currentView !== 'status' }" @click.prevent="currentView = 'status'">Status</a></li>
            <li><a href="#" :class="{ 'secondary': currentView !== 'devices' }" @click.prevent="currentView = 'devices'">Devices</a></li>
            <li><a href="#" :class="{ 'secondary': currentView !== 'settings' }" @click.prevent="currentView = 'settings'">Settings</a></li>
            <li><a href="#" :class="{ 'secondary': currentView !== 'dali' }" @click.prevent="currentView = 'dali'">DALI Control</a></li>
            <li><a href="#" role="button" class="contrast outline" @click.prevent="handleLogout">Logout</a></li>
          </ul>
        </nav>
      </header>

      <StatusPage v-if="currentView === 'status'" />
      <DevicesPage v-if="currentView === 'devices'" />
      <SettingsPage v-if="currentView === 'settings'" />
      <DaliSetup v-if="currentView === 'dali'" />
    </div>
  </main>
</template>