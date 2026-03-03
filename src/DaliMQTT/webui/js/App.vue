<!--
  - Copyright (c) 2026 Alice-Trade Inc.
  - SPDX-License-Identifier: GPL-3.0-or-later
  -->

<script setup lang="ts">
  import { ref, onMounted } from 'vue';
  import { api, setAuth, setAuthToken, clearAuth } from './api';
  import SettingsPage from './components/SettingsPage.vue';
  import DaliBus from './components/DaliBus.vue';
  import DaliAssignments from './components/DaliAssignments.vue';
  import StatusPage from './components/StatusPage.vue';
  import DevicesPage from './components/DevicesPage.vue';

  const loggedIn = ref(false);
  const configured = ref(true);
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
      const res = await api.getInfo();
      configured.value = res.data.configured;

      if (!configured.value) {
        currentView.value = 'settings';
      } else {
        currentView.value = 'status';
      }

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
        const res = await api.getInfo();
        configured.value = res.data.configured;
        if (!configured.value) {
          currentView.value = 'settings';
        }
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
      <header v-if="configured">
        <nav>
          <ul>
            <li><strong>DALI-MQTT Bridge</strong></li>
          </ul>
          <ul>
            <li><a href="#" :class="{ 'secondary': currentView !== 'status' }" @click.prevent="currentView = 'status'">Status</a></li>
            <li><a href="#" :class="{ 'secondary': currentView !== 'devices' }" @click.prevent="currentView = 'devices'">Devices</a></li>
            <li><a href="#" :class="{ 'secondary': currentView !== 'bus' }" @click.prevent="currentView = 'bus'">Bus</a></li>
            <li><a href="#" :class="{ 'secondary': currentView !== 'assignments' }" @click.prevent="currentView = 'assignments'">Assignments</a></li>
            <li><a href="#" :class="{ 'secondary': currentView !== 'settings' }" @click.prevent="currentView = 'settings'">Settings</a></li>
            <li><a href="#" role="button" class="contrast outline" @click.prevent="handleLogout">Logout</a></li>
          </ul>
        </nav>
      </header>

      <div v-else class="provisioning-header">
        <h2>Configuration</h2>
        <p>Please configure your WiFi, MQTT, and System settings to start using the bridge.</p>
      </div>

      <StatusPage v-if="currentView === 'status' && configured" />
      <DevicesPage v-if="currentView === 'devices' && configured" />
      <SettingsPage v-if="currentView === 'settings'" :is-provisioning="!configured" />
      <DaliBus v-if="currentView === 'bus' && configured" />
      <DaliAssignments v-if="currentView === 'assignments' && configured" />
    </div>
  </main>
</template>

<style scoped>
  .provisioning-header {
    text-align: center;
    margin-top: 2rem;
    margin-bottom: 2rem;
  }
  .provisioning-header h2 {
    margin-bottom: 0.5rem;
  }
  .provisioning-header p {
    color: var(--pico-muted-color);
  }
</style>