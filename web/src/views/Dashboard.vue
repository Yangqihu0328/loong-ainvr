<template>
  <div class="dashboard">
    <!-- Stats Cards -->
    <el-row :gutter="16" class="stats-row">
      <el-col :xs="12" :sm="12" :md="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-content">
            <el-icon :size="isMobile ? 28 : 40" color="#409eff"><VideoCamera /></el-icon>
            <div class="stat-info">
              <div class="stat-value">{{ runningChannels }}</div>
              <div class="stat-label">运行通道</div>
            </div>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-content">
            <el-icon :size="isMobile ? 28 : 40" color="#67c23a"><Monitor /></el-icon>
            <div class="stat-info">
              <div class="stat-value">{{ channels.length }}</div>
              <div class="stat-label">总通道数</div>
            </div>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-content">
            <el-icon :size="isMobile ? 28 : 40" color="#e6a23c"><Bell /></el-icon>
            <div class="stat-info">
              <div class="stat-value">{{ totalFramesProcessed }}</div>
              <div class="stat-label">处理帧数</div>
            </div>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-content">
            <el-icon :size="isMobile ? 28 : 40" color="#f56c6c"><Warning /></el-icon>
            <div class="stat-info">
              <div class="stat-value">{{ totalFramesDropped }}</div>
              <div class="stat-label">丢弃帧数</div>
            </div>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <!-- System Resource + Channel List -->
    <el-row :gutter="16">
      <el-col :xs="24" :sm="24" :md="12">
        <el-card>
          <template #header>
            <div class="card-header">
              <span>系统资源监控</span>
              <el-tag size="small" type="info">
                运行 {{ formatUptime(systemStatus.uptime_seconds) }}
              </el-tag>
            </div>
          </template>

          <div class="resource-item">
            <div class="resource-label">
              <span>CPU 使用率</span>
              <span class="resource-value">{{ (systemStatus.cpu_usage_percent || 0).toFixed(1) }}%</span>
            </div>
            <el-progress
              :percentage="Math.min(systemStatus.cpu_usage_percent || 0, 100)"
              :color="progressColor(systemStatus.cpu_usage_percent)"
              :stroke-width="16"
              :show-text="false"
            />
          </div>

          <div class="resource-item">
            <div class="resource-label">
              <span>内存使用</span>
              <span class="resource-value">
                {{ systemStatus.memory_used_mb || 0 }} / {{ systemStatus.memory_total_mb || 0 }} MB
                ({{ (systemStatus.memory_usage_percent || 0).toFixed(1) }}%)
              </span>
            </div>
            <el-progress
              :percentage="Math.min(systemStatus.memory_usage_percent || 0, 100)"
              :color="progressColor(systemStatus.memory_usage_percent)"
              :stroke-width="16"
              :show-text="false"
            />
          </div>

          <template v-if="systemStatus.disks && systemStatus.disks.length">
            <div v-for="disk in systemStatus.disks" :key="disk.path" class="resource-item">
              <div class="resource-label">
                <span>磁盘 {{ disk.path }}</span>
                <span class="resource-value">
                  已用 {{ disk.total_gb - disk.free_gb }} / {{ disk.total_gb }} GB
                  ({{ (disk.usage_percent || 0).toFixed(1) }}%)
                </span>
              </div>
              <el-progress
                :percentage="Math.min(disk.usage_percent || 0, 100)"
                :color="progressColor(disk.usage_percent)"
                :stroke-width="16"
                :show-text="false"
              />
            </div>
          </template>

          <template v-if="systemStatus.gpu">
            <el-divider content-position="left">GPU</el-divider>
            <div class="resource-item">
              <div class="resource-label">
                <span>GPU 使用率</span>
                <span class="resource-value">
                  {{ (systemStatus.gpu.usage_percent || 0).toFixed(1) }}%
                  · {{ systemStatus.gpu.temperature_celsius || 0 }}°C
                </span>
              </div>
              <el-progress
                :percentage="Math.min(systemStatus.gpu.usage_percent || 0, 100)"
                :color="progressColor(systemStatus.gpu.usage_percent)"
                :stroke-width="16"
                :show-text="false"
              />
            </div>
            <div class="resource-item">
              <div class="resource-label">
                <span>GPU 显存</span>
                <span class="resource-value">
                  {{ systemStatus.gpu.memory_used_mb || 0 }} / {{ systemStatus.gpu.memory_total_mb || 0 }} MB
                </span>
              </div>
              <el-progress
                :percentage="systemStatus.gpu.memory_total_mb ? Math.min(systemStatus.gpu.memory_used_mb / systemStatus.gpu.memory_total_mb * 100, 100) : 0"
                :color="progressColor(systemStatus.gpu.memory_total_mb ? systemStatus.gpu.memory_used_mb / systemStatus.gpu.memory_total_mb * 100 : 0)"
                :stroke-width="16"
                :show-text="false"
              />
            </div>
          </template>

          <div class="resource-meta">
            <span>工作线程: {{ systemStatus.process_threads || 0 }}</span>
          </div>
        </el-card>
      </el-col>

      <!-- Channel List -->
      <el-col :xs="24" :sm="24" :md="12">
        <el-card class="channel-card">
          <template #header>
            <div class="card-header">
              <span>通道状态概览</span>
              <el-button type="primary" size="small" @click="refresh">
                <el-icon><Refresh /></el-icon>
                刷新
              </el-button>
            </div>
          </template>

          <el-table :data="channels" stripe v-loading="loading" max-height="420">
            <el-table-column prop="id" label="ID" width="50" />
            <el-table-column prop="name" label="名称" min-width="80" show-overflow-tooltip />
            <el-table-column prop="state" label="状态" width="80">
              <template #default="{ row }">
                <el-tag :type="stateTagType(row.state)" effect="dark" size="small">
                  {{ row.state }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column v-if="!isMobile" prop="frames_processed" label="已处理" width="80" />
            <el-table-column v-if="!isMobile" prop="frames_dropped" label="已丢弃" width="80" />
            <el-table-column label="操作" :width="isMobile ? 100 : 140">
              <template #default="{ row }">
                <el-button
                  v-if="row.state !== 'Running'"
                  type="success" size="small" text
                  @click="startCh(row.id)"
                >启动</el-button>
                <el-button
                  v-else
                  type="warning" size="small" text
                  @click="stopCh(row.id)"
                >停止</el-button>
                <el-button type="danger" size="small" text @click="deleteCh(row.id)">
                  删除
                </el-button>
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup>
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { useChannelStore } from '../stores/channel'
import { useSystemStore } from '../stores/system'

const channelStore = useChannelStore()
const systemStore = useSystemStore()

const channels = computed(() => channelStore.channels)
const loading = computed(() => channelStore.loading)
const systemStatus = computed(() => systemStore.status)

const MOBILE_BREAKPOINT = 768
const isMobile = ref(window.innerWidth < MOBILE_BREAKPOINT)
function onResize() { isMobile.value = window.innerWidth < MOBILE_BREAKPOINT }

let refreshTimer = null

const runningChannels = computed(() =>
  channels.value.filter((c) => c.state === 'Running').length
)

const totalFramesProcessed = computed(() =>
  channels.value.reduce((sum, c) => sum + (c.frames_processed || 0), 0)
)

const totalFramesDropped = computed(() =>
  channels.value.reduce((sum, c) => sum + (c.frames_dropped || 0), 0)
)

function stateTagType(state) {
  const map = {
    Running: 'success',
    Stopped: 'info',
    Error: 'danger',
    Created: 'warning',
    Configured: 'warning',
  }
  return map[state] || 'info'
}

function progressColor(percent) {
  if (percent >= 90) return '#f56c6c'
  if (percent >= 70) return '#e6a23c'
  return '#67c23a'
}

function formatUptime(seconds) {
  if (!seconds) return '0s'
  const d = Math.floor(seconds / 86400)
  const h = Math.floor((seconds % 86400) / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  const parts = []
  if (d > 0) parts.push(`${d}天`)
  if (h > 0) parts.push(`${h}时`)
  parts.push(`${m}分`)
  return parts.join('')
}

function refresh() {
  channelStore.fetchChannels()
  systemStore.fetchStatus()
}

async function startCh(id) {
  try {
    await channelStore.startChannel(id)
  } catch {
    // Error handled by store
  }
}

async function stopCh(id) {
  try {
    await channelStore.stopChannel(id)
  } catch {
    // Error handled by store
  }
}

async function deleteCh(id) {
  try {
    await channelStore.deleteChannel(id)
  } catch {
    // Error handled by store
  }
}

onMounted(() => {
  channelStore.fetchChannels()
  systemStore.fetchStatus()
  refreshTimer = setInterval(() => {
    systemStore.fetchStatus()
  }, 5000)
  window.addEventListener('resize', onResize)
})

onUnmounted(() => {
  if (refreshTimer) {
    clearInterval(refreshTimer)
    refreshTimer = null
  }
  window.removeEventListener('resize', onResize)
})
</script>

<style scoped>
.dashboard {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.stats-row {
  margin-bottom: 0;
}

.stats-row .el-col {
  margin-bottom: 16px;
}

.stat-card {
  cursor: default;
}

.stat-content {
  display: flex;
  align-items: center;
  gap: 12px;
}

.stat-value {
  font-size: 28px;
  font-weight: 700;
  color: #303133;
}

.stat-label {
  font-size: 13px;
  color: #909399;
  margin-top: 2px;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.resource-item {
  margin-bottom: 18px;
}

.resource-label {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 6px;
  font-size: 14px;
  color: #606266;
}

.resource-value {
  font-size: 13px;
  color: #909399;
}

.resource-meta {
  margin-top: 12px;
  font-size: 13px;
  color: #909399;
  text-align: right;
}

.channel-card {
  margin-bottom: 16px;
}

@media (max-width: 768px) {
  .stat-value {
    font-size: 22px;
  }
  .resource-label {
    flex-direction: column;
    align-items: flex-start;
    gap: 2px;
  }
}
</style>
