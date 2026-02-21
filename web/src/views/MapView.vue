<template>
  <div class="map-page">
    <el-card class="map-toolbar" shadow="never">
      <div class="toolbar-row">
        <div class="toolbar-left">
          <el-tag type="info" effect="plain" size="small">
            {{ geoChannels.length }} / {{ channels.length }} 已定位
          </el-tag>
          <el-tag v-if="alertCount > 0" type="danger" effect="dark" size="small" class="alert-badge">
            {{ alertCount }} 告警
          </el-tag>
        </div>
        <div class="toolbar-right">
          <el-button size="small" @click="fitBounds" :disabled="geoChannels.length === 0">
            <el-icon><Position /></el-icon>
            适配视图
          </el-button>
          <el-button size="small" @click="refresh" :loading="loading">
            <el-icon><Refresh /></el-icon>
            刷新
          </el-button>
        </div>
      </div>
    </el-card>

    <div ref="mapContainer" class="map-container" />

    <!-- Channel detail drawer (mobile-friendly) -->
    <el-drawer
      v-model="drawerVisible"
      :title="selectedChannel?.name || '通道详情'"
      :size="isMobile ? '85%' : 360"
      direction="rtl"
    >
      <template v-if="selectedChannel">
        <div class="drawer-content">
          <div class="drawer-status">
            <el-tag :type="stateType(selectedChannel.state)" effect="dark">
              {{ selectedChannel.state }}
            </el-tag>
            <span class="drawer-id">CH{{ selectedChannel.id }}</span>
          </div>

          <el-descriptions :column="1" border size="small" class="drawer-desc">
            <el-descriptions-item label="名称">{{ selectedChannel.name }}</el-descriptions-item>
            <el-descriptions-item label="状态">{{ selectedChannel.state }}</el-descriptions-item>
            <el-descriptions-item label="已处理帧">{{ selectedChannel.frames_processed || 0 }}</el-descriptions-item>
            <el-descriptions-item label="已丢弃帧">{{ selectedChannel.frames_dropped || 0 }}</el-descriptions-item>
            <el-descriptions-item label="纬度">{{ selectedChannel.latitude?.toFixed(6) || '-' }}</el-descriptions-item>
            <el-descriptions-item label="经度">{{ selectedChannel.longitude?.toFixed(6) || '-' }}</el-descriptions-item>
          </el-descriptions>

          <div class="drawer-preview" v-if="selectedChannel.state === 'Running'">
            <NvrPlayer
              :channel-id="selectedChannel.id"
              :channel-name="selectedChannel.name"
              style="width: 100%; aspect-ratio: 16/9;"
            />
          </div>
          <div v-else class="drawer-offline">
            <el-icon :size="48" color="#c0c4cc"><VideoCameraFilled /></el-icon>
            <p>通道未运行</p>
          </div>

          <div class="drawer-actions">
            <el-button
              v-if="selectedChannel.state !== 'Running'"
              type="success" @click="startChannel(selectedChannel.id)"
            >启动通道</el-button>
            <el-button
              v-else
              type="warning" @click="stopChannel(selectedChannel.id)"
            >停止通道</el-button>
            <el-button @click="goToChannel(selectedChannel.id)">
              通道管理
            </el-button>
          </div>
        </div>
      </template>
    </el-drawer>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch, nextTick } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { useChannelStore } from '../stores/channel'
import NvrPlayer from '../components/NvrPlayer.vue'
import L from 'leaflet'
import 'leaflet/dist/leaflet.css'

const router = useRouter()
const channelStore = useChannelStore()
const channels = computed(() => channelStore.channels)
const loading = computed(() => channelStore.loading)

const MOBILE_BREAKPOINT = 768
const isMobile = ref(window.innerWidth < MOBILE_BREAKPOINT)
function onResize() { isMobile.value = window.innerWidth < MOBILE_BREAKPOINT }

const mapContainer = ref(null)
const drawerVisible = ref(false)
const selectedChannel = ref(null)

let map = null
let markerLayer = null
let refreshTimer = null

const geoChannels = computed(() =>
  channels.value.filter(ch => ch.latitude && ch.longitude && (ch.latitude !== 0 || ch.longitude !== 0))
)

const alertCount = computed(() =>
  channels.value.filter(ch => ch.state === 'Error').length
)

function stateType(state) {
  return { Running: 'success', Stopped: 'info', Error: 'danger' }[state] || 'warning'
}

function stateColor(state) {
  return { Running: '#67c23a', Stopped: '#909399', Error: '#f56c6c', Created: '#e6a23c', Configured: '#e6a23c' }[state] || '#909399'
}

function createCameraIcon(state) {
  const color = stateColor(state)
  const isError = state === 'Error'
  const pulse = isError ? `<circle cx="16" cy="16" r="14" fill="none" stroke="${color}" stroke-width="2" opacity="0.6"><animate attributeName="r" from="14" to="22" dur="1.2s" repeatCount="indefinite"/><animate attributeName="opacity" from="0.6" to="0" dur="1.2s" repeatCount="indefinite"/></circle>` : ''

  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">
    ${pulse}
    <circle cx="16" cy="16" r="12" fill="${color}" stroke="#fff" stroke-width="2"/>
    <path d="M10 12h8v6h-8z M18 13l4-2v8l-4-2z" fill="#fff" fill-rule="evenodd"/>
  </svg>`

  return L.divIcon({
    html: svg,
    className: 'camera-marker' + (isError ? ' marker-alert' : ''),
    iconSize: [32, 32],
    iconAnchor: [16, 16],
    popupAnchor: [0, -18],
  })
}

function initMap() {
  if (!mapContainer.value || map) return

  map = L.map(mapContainer.value, {
    center: [39.9042, 116.4074],
    zoom: 10,
    zoomControl: !isMobile.value,
    attributionControl: true,
  })

  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    attribution: '&copy; <a href="https://www.openstreetmap.org/">OpenStreetMap</a>',
    maxZoom: 19,
  }).addTo(map)

  markerLayer = L.layerGroup().addTo(map)

  if (isMobile.value) {
    L.control.zoom({ position: 'bottomright' }).addTo(map)
  }
}

function updateMarkers() {
  if (!markerLayer) return
  markerLayer.clearLayers()

  for (const ch of geoChannels.value) {
    const marker = L.marker([ch.latitude, ch.longitude], {
      icon: createCameraIcon(ch.state),
      title: ch.name,
    })

    marker.bindTooltip(ch.name, {
      direction: 'top',
      offset: [0, -18],
      className: 'camera-tooltip',
    })

    marker.on('click', () => {
      selectedChannel.value = ch
      drawerVisible.value = true
    })

    marker.addTo(markerLayer)
  }
}

function fitBounds() {
  if (!map || geoChannels.value.length === 0) return
  const bounds = L.latLngBounds(geoChannels.value.map(ch => [ch.latitude, ch.longitude]))
  map.fitBounds(bounds.pad(0.15))
}

function refresh() {
  channelStore.fetchChannels()
}

async function startChannel(id) {
  try {
    await channelStore.startChannel(id)
    ElMessage.success('通道已启动')
  } catch {
    ElMessage.error('启动失败')
  }
}

async function stopChannel(id) {
  try {
    await channelStore.stopChannel(id)
    ElMessage.success('通道已停止')
  } catch {
    ElMessage.error('停止失败')
  }
}

function goToChannel(id) {
  drawerVisible.value = false
  router.push(`/channels`)
}

watch(channels, () => {
  updateMarkers()
  if (selectedChannel.value) {
    const updated = channels.value.find(c => c.id === selectedChannel.value.id)
    if (updated) selectedChannel.value = updated
  }
})

onMounted(async () => {
  await channelStore.fetchChannels()
  await nextTick()
  initMap()
  updateMarkers()

  if (geoChannels.value.length > 0) {
    fitBounds()
  }

  refreshTimer = setInterval(refresh, 15000)
  window.addEventListener('resize', onResize)
  window.addEventListener('resize', () => map?.invalidateSize())
})

onUnmounted(() => {
  if (refreshTimer) clearInterval(refreshTimer)
  window.removeEventListener('resize', onResize)
  if (map) {
    map.remove()
    map = null
  }
})
</script>

<style scoped>
.map-page {
  display: flex;
  flex-direction: column;
  height: calc(100vh - 120px);
  gap: 0;
}

.map-toolbar {
  flex-shrink: 0;
  border-radius: 0;
}

.map-toolbar :deep(.el-card__body) {
  padding: 8px 16px;
}

.toolbar-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 8px;
}

.toolbar-left,
.toolbar-right {
  display: flex;
  align-items: center;
  gap: 8px;
}

.alert-badge {
  animation: pulse-badge 1.5s ease-in-out infinite;
}

@keyframes pulse-badge {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}

.map-container {
  flex: 1;
  min-height: 300px;
  z-index: 0;
}

.drawer-content {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.drawer-status {
  display: flex;
  align-items: center;
  gap: 8px;
}

.drawer-id {
  font-size: 13px;
  color: #909399;
}

.drawer-desc {
  width: 100%;
}

.drawer-preview {
  border-radius: 6px;
  overflow: hidden;
  background: #000;
}

.drawer-offline {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 32px 0;
  color: #c0c4cc;
}

.drawer-offline p {
  margin-top: 8px;
  font-size: 13px;
}

.drawer-actions {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}

@media (max-width: 768px) {
  .map-page {
    height: calc(100vh - 100px);
  }
}
</style>

<style>
.camera-marker {
  background: none !important;
  border: none !important;
}

.marker-alert {
  animation: marker-pulse 1.5s ease-in-out infinite;
}

@keyframes marker-pulse {
  0%, 100% { transform: scale(1); }
  50% { transform: scale(1.2); }
}

.camera-tooltip {
  font-size: 12px;
  font-weight: 500;
  padding: 4px 8px;
  border-radius: 4px;
}
</style>
