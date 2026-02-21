import { createRouter, createWebHistory } from 'vue-router'

const routes = [
  {
    path: '/login',
    name: 'Login',
    component: () => import('../views/Login.vue'),
    meta: { public: true },
  },
  {
    path: '/',
    component: () => import('../views/Layout.vue'),
    redirect: '/dashboard',
    children: [
      {
        path: 'dashboard',
        name: 'Dashboard',
        component: () => import('../views/Dashboard.vue'),
        meta: { title: '仪表板', icon: 'Monitor' },
      },
      {
        path: 'channels',
        name: 'Channels',
        component: () => import('../views/Channels.vue'),
        meta: { title: '通道管理', icon: 'VideoCamera' },
      },
      {
        path: 'live',
        name: 'LiveView',
        component: () => import('../views/LiveView.vue'),
        meta: { title: '实时预览', icon: 'View' },
      },
      {
        path: 'recordings',
        name: 'Recordings',
        component: () => import('../views/Recordings.vue'),
        meta: { title: '录像回放', icon: 'VideoPlay' },
      },
      {
        path: 'events',
        name: 'Events',
        component: () => import('../views/Events.vue'),
        meta: { title: '报警事件', icon: 'Bell' },
      },
      {
        path: 'rules',
        name: 'Rules',
        component: () => import('../views/Rules.vue'),
        meta: { title: '智能规则', icon: 'Guide' },
      },
      {
        path: 'plates',
        name: 'LicensePlates',
        component: () => import('../views/LicensePlates.vue'),
        meta: { title: '车牌识别', icon: 'Postcard' },
      },
      {
        path: 'faces',
        name: 'FaceAnalytics',
        component: () => import('../views/FaceAnalytics.vue'),
        meta: { title: '人脸分析', icon: 'Avatar' },
      },
      {
        path: 'analytics',
        name: 'Analytics',
        component: () => import('../views/Analytics.vue'),
        meta: { title: '数据分析', icon: 'DataAnalysis' },
      },
      {
        path: 'map',
        name: 'MapView',
        component: () => import('../views/MapView.vue'),
        meta: { title: '地图视图', icon: 'MapLocation' },
      },
      {
        path: 'devices',
        name: 'Devices',
        component: () => import('../views/Devices.vue'),
        meta: { title: '设备发现', icon: 'Cpu' },
      },
      {
        path: 'plugins',
        name: 'Plugins',
        component: () => import('../views/Plugins.vue'),
        meta: { title: '模型插件', icon: 'Connection', requireAdmin: true },
      },
      {
        path: 'users',
        name: 'Users',
        component: () => import('../views/Users.vue'),
        meta: { title: '用户管理', icon: 'UserFilled', requireAdmin: true },
      },
      {
        path: 'settings',
        name: 'Settings',
        component: () => import('../views/Settings.vue'),
        meta: { title: '系统设置', icon: 'Setting' },
      },
    ],
  },
]

const router = createRouter({
  history: createWebHistory(),
  routes,
})

// Navigation guard: require authentication
router.beforeEach((to, from, next) => {
  const token = localStorage.getItem('token')
  let user = null
  try {
    user = JSON.parse(localStorage.getItem('user') || 'null')
  } catch {
    localStorage.removeItem('user')
  }

  // Public routes (login page)
  if (to.meta.public) {
    if (token) {
      next('/')
    } else {
      next()
    }
    return
  }

  // Require authentication
  if (!token) {
    next('/login')
    return
  }

  // Admin-only routes
  if (to.meta.requireAdmin && user?.role !== 'admin') {
    next('/dashboard')
    return
  }

  next()
})

export default router
