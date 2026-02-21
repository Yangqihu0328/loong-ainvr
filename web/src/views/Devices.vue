<template>
  <div class="devices-page">
    <el-card>
      <template #header>
        <div class="card-header">
          <span>ONVIF 设备发现</span>
          <el-button type="primary" @click="handleDiscover" :loading="discovering">
            <el-icon><Search /></el-icon>
            扫描设备
          </el-button>
        </div>
      </template>

      <el-alert
        v-if="!discovered.length && !discovering"
        title="点击「扫描设备」自动发现局域网中的 ONVIF 摄像机"
        type="info"
        :closable="false"
        show-icon
        style="margin-bottom: 16px"
      />

      <el-table :data="discovered" stripe v-loading="discovering" empty-text="未发现设备，请点击「扫描设备」">
        <el-table-column prop="ip_address" label="设备地址" min-width="160" />
        <el-table-column prop="manufacturer" label="厂商" width="140" />
        <el-table-column prop="model" label="型号" width="140" />
        <el-table-column prop="xaddr" label="服务地址" min-width="200" show-overflow-tooltip />
        <el-table-column label="操作" width="260" fixed="right">
          <template #default="{ row }">
            <el-button-group size="small">
              <el-button type="primary" @click="handleGetInfo(row)">
                获取信息
              </el-button>
              <el-button type="success" @click="handleAddChannel(row)">
                添加为通道
              </el-button>
              <el-button type="warning" @click="handlePtz(row)">
                PTZ 控制
              </el-button>
            </el-button-group>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <!-- Device Info Dialog -->
    <el-dialog v-model="showInfoDialog" title="设备详细信息" width="520px">
      <el-descriptions :column="1" border v-loading="infoLoading">
        <el-descriptions-item label="地址">{{ deviceInfo.address }}</el-descriptions-item>
        <el-descriptions-item label="厂商">{{ deviceInfo.manufacturer || '-' }}</el-descriptions-item>
        <el-descriptions-item label="型号">{{ deviceInfo.model || '-' }}</el-descriptions-item>
        <el-descriptions-item label="固件版本">{{ deviceInfo.firmware_version || '-' }}</el-descriptions-item>
        <el-descriptions-item label="序列号">{{ deviceInfo.serial_number || '-' }}</el-descriptions-item>
        <el-descriptions-item label="RTSP 地址">
          <template v-if="deviceInfo.profiles && deviceInfo.profiles.length">
            <div v-for="p in deviceInfo.profiles" :key="p.token" style="margin-bottom: 4px">
              <el-tag type="success" size="small">{{ p.name }}: {{ p.stream_uri }}</el-tag>
            </div>
          </template>
          <span v-else class="text-muted">未获取到</span>
        </el-descriptions-item>
      </el-descriptions>
      <template #footer>
        <el-button @click="showInfoDialog = false">关闭</el-button>
        <el-button
          type="success"
          @click="handleAddChannelFromInfo"
          :disabled="!firstStreamUri"
        >
          添加为通道
        </el-button>
      </template>
    </el-dialog>

    <!-- PTZ Control Dialog -->
    <el-dialog v-model="showPtzDialog" title="PTZ 云台控制" width="400px">
      <div class="ptz-controls">
        <div class="ptz-grid">
          <div></div>
          <el-button circle @click="ptzMove('up')">
            <el-icon><Top /></el-icon>
          </el-button>
          <div></div>
          <el-button circle @click="ptzMove('left')">
            <el-icon><Back /></el-icon>
          </el-button>
          <el-button circle type="danger" @click="ptzMove('stop')">
            <el-icon><CloseBold /></el-icon>
          </el-button>
          <el-button circle @click="ptzMove('right')">
            <el-icon><Right /></el-icon>
          </el-button>
          <div></div>
          <el-button circle @click="ptzMove('down')">
            <el-icon><Bottom /></el-icon>
          </el-button>
          <div></div>
        </div>
        <div class="ptz-zoom">
          <el-button @click="ptzMove('zoom_in')">变焦+</el-button>
          <el-button @click="ptzMove('zoom_out')">变焦-</el-button>
        </div>
        <div class="ptz-speed">
          <span>速度:</span>
          <el-slider v-model="ptzSpeed" :min="0.1" :max="1.0" :step="0.1" style="flex: 1" />
        </div>
      </div>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { ElMessage } from 'element-plus'
import { useRouter } from 'vue-router'
import { onvifApi } from '../api'

const router = useRouter()

const discovering = ref(false)
const discovered = ref([])

const firstStreamUri = computed(() => {
  const profiles = deviceInfo.value?.profiles
  if (profiles && profiles.length > 0) {
    return profiles[0].stream_uri || ''
  }
  return ''
})

const showInfoDialog = ref(false)
const infoLoading = ref(false)
const deviceInfo = ref({})

const showPtzDialog = ref(false)
const ptzAddress = ref('')
const ptzSpeed = ref(0.5)

async function handleDiscover() {
  discovering.value = true
  try {
    const { data } = await onvifApi.discover()
    discovered.value = data.devices || data || []
    if (!discovered.value.length) {
      ElMessage.info('未发现 ONVIF 设备')
    } else {
      ElMessage.success(`发现 ${discovered.value.length} 个设备`)
    }
  } catch (err) {
    ElMessage.error('设备扫描失败: ' + (err.response?.data?.error || err.message))
  } finally {
    discovering.value = false
  }
}

async function handleGetInfo(row) {
  showInfoDialog.value = true
  infoLoading.value = true
  const addr = row.ip_address || row.xaddr
  deviceInfo.value = { address: addr }
  try {
    const { data } = await onvifApi.deviceInfo(addr)
    deviceInfo.value = { address: addr, ...data }
  } catch (err) {
    ElMessage.error('获取设备信息失败')
  } finally {
    infoLoading.value = false
  }
}

function handleAddChannel(row) {
  const addr = row.ip_address || row.xaddr
  router.push({
    path: '/channels',
    query: {
      add: '1',
      name: row.manufacturer ? `${row.manufacturer} ${row.model || ''}`.trim() : addr,
      rtsp_url: row.rtsp_url || '',
    },
  })
}

function handleAddChannelFromInfo() {
  const info = deviceInfo.value
  router.push({
    path: '/channels',
    query: {
      add: '1',
      name: info.manufacturer ? `${info.manufacturer} ${info.model || ''}`.trim() : info.address,
      rtsp_url: firstStreamUri.value,
    },
  })
}

function handlePtz(row) {
  ptzAddress.value = row.ip_address || row.xaddr
  showPtzDialog.value = true
}

async function ptzMove(direction) {
  try {
    await onvifApi.ptzControl(ptzAddress.value, {
      action: direction,
      speed: ptzSpeed.value,
    })
  } catch (err) {
    ElMessage.error('PTZ 控制失败')
  }
}
</script>

<style scoped>
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.text-muted {
  color: #909399;
}

.ptz-controls {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 20px;
}

.ptz-grid {
  display: grid;
  grid-template-columns: 48px 48px 48px;
  grid-template-rows: 48px 48px 48px;
  gap: 4px;
  justify-items: center;
  align-items: center;
}

.ptz-zoom {
  display: flex;
  gap: 12px;
}

.ptz-speed {
  display: flex;
  align-items: center;
  gap: 12px;
  width: 100%;
}
</style>
