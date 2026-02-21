<template>
  <!-- SW Update Banner -->
  <transition name="slide-down">
    <div v-if="swUpdateAvailable" class="pwa-banner update-banner">
      <span>新版本可用</span>
      <el-button type="primary" size="small" @click="handleUpdate">立即更新</el-button>
      <el-button size="small" text @click="dismissUpdate">稍后</el-button>
    </div>
  </transition>

  <!-- Install Banner -->
  <transition name="slide-up">
    <div v-if="showInstall" class="pwa-banner install-banner">
      <div class="install-info">
        <el-icon :size="20"><Monitor /></el-icon>
        <span>将 Loong NVR 添加到主屏幕</span>
      </div>
      <div class="install-actions">
        <el-button type="primary" size="small" @click="handleInstall">安装</el-button>
        <el-button size="small" text @click="dismissInstall">忽略</el-button>
      </div>
    </div>
  </transition>
</template>

<script setup>
import { ref, computed } from 'vue'
import { swUpdateAvailable, applySwUpdate, canInstall, promptInstall } from '../utils/pwa'

const updateDismissed = ref(false)
const installDismissed = ref(false)

const showInstall = computed(() =>
  canInstall.value && !installDismissed.value
)

function handleUpdate() {
  applySwUpdate()
}

function dismissUpdate() {
  updateDismissed.value = true
}

async function handleInstall() {
  await promptInstall()
}

function dismissInstall() {
  installDismissed.value = true
}
</script>

<style scoped>
.pwa-banner {
  position: fixed;
  left: 50%;
  transform: translateX(-50%);
  z-index: 3000;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 20px;
  border-radius: 8px;
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.15);
  backdrop-filter: blur(8px);
  max-width: calc(100vw - 32px);
}

.update-banner {
  top: 16px;
  background: rgba(64, 158, 255, 0.95);
  color: #fff;
}

.install-banner {
  bottom: 16px;
  background: rgba(255, 255, 255, 0.96);
  color: #303133;
  border: 1px solid #e4e7ed;
}

.install-info {
  display: flex;
  align-items: center;
  gap: 8px;
}

.install-actions {
  display: flex;
  gap: 4px;
}

.slide-down-enter-active,
.slide-down-leave-active {
  transition: all 0.3s ease;
}
.slide-down-enter-from,
.slide-down-leave-to {
  opacity: 0;
  transform: translateX(-50%) translateY(-20px);
}

.slide-up-enter-active,
.slide-up-leave-active {
  transition: all 0.3s ease;
}
.slide-up-enter-from,
.slide-up-leave-to {
  opacity: 0;
  transform: translateX(-50%) translateY(20px);
}

@media (max-width: 768px) {
  .pwa-banner {
    flex-wrap: wrap;
    justify-content: center;
    text-align: center;
  }
}
</style>
