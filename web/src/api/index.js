import axios from 'axios'

const api = axios.create({
  baseURL: '/api',
  timeout: 10000,
})

// Request interceptor: attach JWT token
api.interceptors.request.use((config) => {
  const token = localStorage.getItem('token')
  if (token) {
    config.headers.Authorization = `Bearer ${token}`
  }
  return config
})

// Response interceptor: handle 401 (redirect to login)
api.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error.response?.status === 401) {
      localStorage.removeItem('token')
      localStorage.removeItem('user')
      if (window.location.pathname !== '/login') {
        window.location.href = '/login'
      }
    }
    return Promise.reject(error)
  }
)

// Auth API
export const authApi = {
  login: (username, password) =>
    api.post('/auth/login', { username, password }),
  profile: () => api.get('/auth/profile'),
  changePassword: (old_password, new_password) =>
    api.put('/auth/password', { old_password, new_password }),
}

// Users API (admin only)
export const userApi = {
  list: () => api.get('/users'),
  create: (data) => api.post('/users', data),
  update: (id, data) => api.put(`/users/${id}`, data),
  delete: (id) => api.delete(`/users/${id}`),
}

// Channels API
export const channelApi = {
  list: () => api.get('/channels'),
  get: (id) => api.get(`/channels/${id}`),
  create: (data) => api.post('/channels', data),
  update: (id, data) => api.put(`/channels/${id}`, data),
  delete: (id) => api.delete(`/channels/${id}`),
  start: (id) => api.post(`/channels/${id}/start`),
  stop: (id) => api.post(`/channels/${id}/stop`),
  getOverlay: (id) => api.get(`/channels/${id}/overlay`),
  updateOverlay: (id, data) => api.put(`/channels/${id}/overlay`, data),
}

// Recordings API
export const recordingApi = {
  query: (params) => api.get('/recordings', { params }),
  events: (params) => api.get('/events', { params }),
  timeline: (params) => api.get('/recordings/timeline', { params }),
  searchEvents: (params) => api.get('/events/search', { params }),
}

// System API
export const systemApi = {
  status: () => api.get('/system/status'),
  updateStorage: (data) => api.put('/system/storage', data),
  getConfig: () => api.get('/system/config'),
  updateConfig: (data) => api.put('/system/config', data),
}

// ONVIF API
export const onvifApi = {
  discover: () => api.get('/onvif/discover'),
  deviceInfo: (addr) => api.get(`/onvif/device/${encodeURIComponent(addr)}`),
  ptzControl: (addr, data) => api.post(`/onvif/device/${encodeURIComponent(addr)}/ptz`, data),
}

// Analytics API
export const analyticsApi = {
  summary: (params) => api.get('/analytics/summary', { params }),
  trends: (params) => api.get('/analytics/trends', { params }),
  heatmap: (params) => api.get('/analytics/heatmap', { params }),
  peakHours: (params) => api.get('/analytics/peak-hours', { params }),
  counting: (params) => api.get('/analytics/counting', { params }),
}

// Rules API
export const rulesApi = {
  list: (params) => api.get('/rules', { params }),
  get: (id) => api.get(`/rules/${id}`),
  create: (data) => api.post('/rules', data),
  update: (id, data) => api.put(`/rules/${id}`, data),
  delete: (id) => api.delete(`/rules/${id}`),
  events: (params) => api.get('/rules/events', { params }),
}

// License Plate API
export const plateApi = {
  query: (params) => api.get('/analytics/plates', { params }),
}

// Face Analytics API
export const faceApi = {
  query: (params) => api.get('/analytics/faces', { params }),
  search: (data) => api.post('/analytics/faces/search', data),
}

// Notification API
export const notificationApi = {
  getConfig: () => api.get('/system/notifications'),
  updateConfig: (data) => api.put('/system/notifications', data),
  test: (data) => api.post('/system/notifications/test', data),
  history: (params) => api.get('/system/notifications/history', { params }),
}

// WebRTC API
export const webrtcApi = {
  offer: (channel_id, sdp) => api.post('/webrtc/offer', { channel_id, sdp }),
  ice: (session_id, candidate) =>
    api.post('/webrtc/ice', { session_id, candidate }),
  close: (session_id) => api.delete(`/webrtc/${session_id}`),
}

// Plugin API
export const pluginApi = {
  list: () => api.get('/plugins'),
  upload: (formData) => api.post('/plugins/upload', formData, {
    headers: { 'Content-Type': 'multipart/form-data' },
  }),
}

// MQTT API
export const mqttApi = {
  getConfig: () => api.get('/mqtt/config'),
  updateConfig: (data) => api.put('/mqtt/config', data),
  test: () => api.post('/mqtt/test'),
}

// Cloud Backup API
export const backupApi = {
  getConfig: () => api.get('/backup/config'),
  updateConfig: (data) => api.put('/backup/config', data),
  getStatus: () => api.get('/backup/status'),
  test: () => api.post('/backup/test'),
  trigger: () => api.post('/backup/trigger'),
}

// Alarm API
export const alarmApi = {
  listRules: (params) => api.get('/alarms/rules', { params }),
  createRule: (data) => api.post('/alarms/rules', data),
  updateRule: (id, data) => api.put(`/alarms/rules/${id}`, data),
  deleteRule: (id) => api.delete(`/alarms/rules/${id}`),
  query: (params) => api.get('/alarms', { params }),
  acknowledge: (id) => api.post(`/alarms/${id}/acknowledge`),
  acknowledgeAll: () => api.post('/alarms/acknowledge-all'),
}

export default api
