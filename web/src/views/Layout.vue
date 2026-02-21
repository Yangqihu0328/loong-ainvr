<template>
  <el-container class="layout-container">
    <!-- Desktop Sidebar -->
    <el-aside
      v-if="!isMobile"
      :width="isCollapse ? '64px' : '220px'"
      class="sidebar"
    >
      <div class="logo">
        <el-icon :size="28"><Monitor /></el-icon>
        <span v-show="!isCollapse" class="logo-text">Loong AI NVR</span>
      </div>
      <el-menu
        :default-active="$route.path"
        router
        :collapse="isCollapse"
        background-color="#1d1e1f"
        text-color="#bfcbd9"
        active-text-color="#409eff"
      >
        <el-menu-item
          v-for="route in visibleRoutes"
          :key="route.path"
          :index="'/' + route.path"
        >
          <el-icon><component :is="route.meta.icon" /></el-icon>
          <template #title>{{ route.meta.title }}</template>
        </el-menu-item>
      </el-menu>
    </el-aside>

    <!-- Mobile Drawer -->
    <el-drawer
      v-model="drawerVisible"
      direction="ltr"
      :size="260"
      :with-header="false"
      class="mobile-drawer"
    >
      <div class="drawer-logo">
        <el-icon :size="24"><Monitor /></el-icon>
        <span class="logo-text">Loong AI NVR</span>
      </div>
      <el-menu
        :default-active="$route.path"
        router
        background-color="#1d1e1f"
        text-color="#bfcbd9"
        active-text-color="#409eff"
        @select="drawerVisible = false"
      >
        <el-menu-item
          v-for="route in visibleRoutes"
          :key="route.path"
          :index="'/' + route.path"
        >
          <el-icon><component :is="route.meta.icon" /></el-icon>
          <template #title>{{ route.meta.title }}</template>
        </el-menu-item>
      </el-menu>
    </el-drawer>

    <!-- Main Content -->
    <el-container>
      <el-header class="header">
        <div class="header-left">
          <el-icon
            class="collapse-btn"
            :size="20"
            @click="isMobile ? (drawerVisible = true) : (isCollapse = !isCollapse)"
          >
            <Fold v-if="!isMobile && !isCollapse" />
            <Expand v-else />
          </el-icon>
          <el-breadcrumb v-if="!isMobile" separator="/">
            <el-breadcrumb-item :to="{ path: '/' }">首页</el-breadcrumb-item>
            <el-breadcrumb-item>{{ $route.meta.title }}</el-breadcrumb-item>
          </el-breadcrumb>
          <span v-else class="mobile-title">{{ $route.meta.title }}</span>
        </div>
        <div class="header-right">
          <template v-if="!isMobile">
            <el-tag type="success" effect="dark" size="small">
              v{{ systemStatus.version || '0.1.0' }}
            </el-tag>
            <el-tag type="info" effect="dark" size="small">
              {{ systemStatus.channels_active || 0 }} / {{ systemStatus.max_channels || 64 }} 通道
            </el-tag>
          </template>

          <!-- User Dropdown -->
          <el-dropdown trigger="click" @command="handleUserCommand">
            <span class="user-info">
              <el-icon><UserFilled /></el-icon>
              <span v-if="!isMobile" class="username">{{ authStore.user?.display_name || authStore.user?.username || '用户' }}</span>
              <el-tag
                v-if="!isMobile"
                :type="roleTagType"
                effect="plain"
                size="small"
                style="margin-left: 4px"
              >
                {{ roleLabel }}
              </el-tag>
              <el-icon class="el-icon--right"><ArrowDown /></el-icon>
            </span>
            <template #dropdown>
              <el-dropdown-menu>
                <el-dropdown-item command="profile">
                  <el-icon><User /></el-icon>个人信息
                </el-dropdown-item>
                <el-dropdown-item command="password">
                  <el-icon><Key /></el-icon>修改密码
                </el-dropdown-item>
                <el-dropdown-item divided command="logout">
                  <el-icon><SwitchButton /></el-icon>退出登录
                </el-dropdown-item>
              </el-dropdown-menu>
            </template>
          </el-dropdown>
        </div>
      </el-header>

      <el-main class="main-content">
        <router-view />
      </el-main>
    </el-container>

    <!-- Change Password Dialog -->
    <el-dialog
      v-model="showPasswordDialog"
      title="修改密码"
      :width="isMobile ? '92%' : '420px'"
    >
      <el-form
        ref="passwordFormRef"
        :model="passwordForm"
        :rules="passwordRules"
        label-width="80px"
      >
        <el-form-item label="旧密码" prop="oldPassword">
          <el-input
            v-model="passwordForm.oldPassword"
            type="password"
            show-password
          />
        </el-form-item>
        <el-form-item label="新密码" prop="newPassword">
          <el-input
            v-model="passwordForm.newPassword"
            type="password"
            show-password
          />
        </el-form-item>
        <el-form-item label="确认密码" prop="confirmPassword">
          <el-input
            v-model="passwordForm.confirmPassword"
            type="password"
            show-password
          />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showPasswordDialog = false">取消</el-button>
        <el-button type="primary" :loading="passwordLoading" @click="submitPasswordChange">
          确定
        </el-button>
      </template>
    </el-dialog>
  </el-container>
</template>

<script setup>
import { ref, computed, reactive, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { useSystemStore } from '../stores/system'
import { useAuthStore } from '../stores/auth'

const router = useRouter()
const systemStore = useSystemStore()
const authStore = useAuthStore()
const isCollapse = ref(false)
const drawerVisible = ref(false)

const MOBILE_BREAKPOINT = 768
const isMobile = ref(window.innerWidth < MOBILE_BREAKPOINT)

function onResize() {
  isMobile.value = window.innerWidth < MOBILE_BREAKPOINT
  if (!isMobile.value) drawerVisible.value = false
}

const menuRoutes = computed(() => {
  const mainRoute = router.options.routes.find((r) => r.path === '/')
  return mainRoute?.children || []
})

const visibleRoutes = computed(() => {
  return menuRoutes.value.filter((route) => {
    if (route.meta?.requireAdmin && authStore.user?.role !== 'admin') {
      return false
    }
    return true
  })
})

const systemStatus = computed(() => systemStore.status)

const roleLabel = computed(() => {
  const roleMap = { admin: '管理员', operator: '操作员', viewer: '观察者' }
  return roleMap[authStore.user?.role] || '观察者'
})

const roleTagType = computed(() => {
  const typeMap = { admin: 'danger', operator: 'warning', viewer: 'info' }
  return typeMap[authStore.user?.role] || 'info'
})

// Password change dialog
const showPasswordDialog = ref(false)
const passwordLoading = ref(false)
const passwordFormRef = ref(null)

const passwordForm = reactive({
  oldPassword: '',
  newPassword: '',
  confirmPassword: '',
})

const passwordRules = {
  oldPassword: [{ required: true, message: '请输入旧密码', trigger: 'blur' }],
  newPassword: [
    { required: true, message: '请输入新密码', trigger: 'blur' },
    { min: 6, message: '密码至少6个字符', trigger: 'blur' },
  ],
  confirmPassword: [
    { required: true, message: '请确认新密码', trigger: 'blur' },
    {
      validator: (rule, value, callback) => {
        if (value !== passwordForm.newPassword) {
          callback(new Error('两次密码不一致'))
        } else {
          callback()
        }
      },
      trigger: 'blur',
    },
  ],
}

function handleUserCommand(command) {
  if (command === 'profile') {
    ElMessage.info(
      `${authStore.user?.display_name || authStore.user?.username} (${roleLabel.value})`
    )
  } else if (command === 'password') {
    passwordForm.oldPassword = ''
    passwordForm.newPassword = ''
    passwordForm.confirmPassword = ''
    showPasswordDialog.value = true
  } else if (command === 'logout') {
    ElMessageBox.confirm('确定要退出登录吗？', '提示', {
      confirmButtonText: '确定',
      cancelButtonText: '取消',
      type: 'warning',
    }).then(() => {
      authStore.logout()
      router.push('/login')
      ElMessage.success('已退出登录')
    }).catch(() => {})
  }
}

async function submitPasswordChange() {
  if (!passwordFormRef.value) return
  try {
    await passwordFormRef.value.validate()
  } catch {
    return
  }

  passwordLoading.value = true
  try {
    await authStore.changePassword(
      passwordForm.oldPassword,
      passwordForm.newPassword
    )
    showPasswordDialog.value = false
    ElMessage.success('密码修改成功')
  } catch (err) {
    const msg =
      err.response?.data?.error === 'incorrect old password'
        ? '旧密码不正确'
        : '修改失败，请重试'
    ElMessage.error(msg)
  } finally {
    passwordLoading.value = false
  }
}

let statusTimer = null

onMounted(() => {
  systemStore.fetchStatus()
  authStore.fetchProfile()
  statusTimer = setInterval(() => systemStore.fetchStatus(), 10000)
  window.addEventListener('resize', onResize)
})

onUnmounted(() => {
  if (statusTimer) {
    clearInterval(statusTimer)
    statusTimer = null
  }
  window.removeEventListener('resize', onResize)
})
</script>

<style scoped>
.layout-container {
  height: 100vh;
}

.sidebar {
  background-color: #1d1e1f;
  transition: width 0.3s;
  overflow: hidden;
}

.logo,
.drawer-logo {
  display: flex;
  align-items: center;
  justify-content: center;
  height: 60px;
  color: #409eff;
  gap: 8px;
  border-bottom: 1px solid #333;
}

.logo-text {
  font-size: 16px;
  font-weight: 600;
  white-space: nowrap;
}

.header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  background: #fff;
  border-bottom: 1px solid #e4e7ed;
  padding: 0 20px;
  height: 60px;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 16px;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 8px;
}

.collapse-btn {
  cursor: pointer;
  color: #606266;
}

.collapse-btn:hover {
  color: #409eff;
}

.mobile-title {
  font-size: 16px;
  font-weight: 600;
  color: #303133;
}

.user-info {
  display: flex;
  align-items: center;
  gap: 4px;
  cursor: pointer;
  color: #606266;
  padding: 4px 8px;
  border-radius: 4px;
  transition: background-color 0.2s;
}

.user-info:hover {
  background-color: #f5f7fa;
  color: #409eff;
}

.username {
  font-size: 14px;
  max-width: 100px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.main-content {
  background-color: #f5f7fa;
  padding: 20px;
}

@media (max-width: 768px) {
  .header {
    padding: 0 12px;
  }
  .main-content {
    padding: 12px;
  }
}
</style>

<style>
/* Global drawer override (not scoped) */
.mobile-drawer .el-drawer__body {
  padding: 0;
  background-color: #1d1e1f;
}
</style>
