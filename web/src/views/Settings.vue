<template>
  <div class="settings-page">
    <el-row :gutter="20">
      <!-- System Info -->
      <el-col :span="12">
        <el-card>
          <template #header>系统信息</template>
          <el-descriptions :column="1" border>
            <el-descriptions-item label="版本">
              {{ systemStatus.version }}
            </el-descriptions-item>
            <el-descriptions-item label="活跃通道">
              {{ systemStatus.channels_active }}
            </el-descriptions-item>
            <el-descriptions-item label="最大通道">
              {{ systemStatus.max_channels }}
            </el-descriptions-item>
          </el-descriptions>
        </el-card>
      </el-col>

      <!-- Storage Settings -->
      <el-col :span="12">
        <el-card>
          <template #header>存储设置</template>
          <el-form label-width="120px">
            <el-form-item label="录像路径">
              <el-input v-model="storageConfig.path" disabled />
            </el-form-item>
            <el-form-item label="分段时长">
              <el-input-number
                v-model="storageConfig.segment_minutes"
                :min="5" :max="120"
              />
              <span style="margin-left: 8px">分钟</span>
            </el-form-item>
            <el-form-item label="保留天数">
              <el-input-number
                v-model="storageConfig.retention_days"
                :min="1" :max="365"
              />
            </el-form-item>
            <el-form-item label="最大空间">
              <el-input-number
                v-model="storageConfig.max_size_gb"
                :min="10" :max="10000"
              />
              <span style="margin-left: 8px">GB</span>
            </el-form-item>
            <el-form-item>
              <el-button type="primary" @click="handleSaveStorage" :loading="saving">
                保存设置
              </el-button>
            </el-form-item>
          </el-form>
        </el-card>
      </el-col>
    </el-row>

    <!-- Notification Settings -->
    <el-row :gutter="20" style="margin-top: 20px">
      <!-- SMTP -->
      <el-col :span="12">
        <el-card>
          <template #header>
            <div style="display: flex; justify-content: space-between; align-items: center;">
              <span>邮件通知 (SMTP)</span>
              <el-switch v-model="smtpConfig.enabled" />
            </div>
          </template>
          <el-form label-width="120px" :disabled="!smtpConfig.enabled">
            <el-form-item label="SMTP 服务器">
              <el-input v-model="smtpConfig.host" placeholder="smtp.example.com" />
            </el-form-item>
            <el-form-item label="端口">
              <el-input-number v-model="smtpConfig.port" :min="1" :max="65535" />
            </el-form-item>
            <el-form-item label="使用 TLS">
              <el-switch v-model="smtpConfig.use_tls" />
            </el-form-item>
            <el-form-item label="用户名">
              <el-input v-model="smtpConfig.username" />
            </el-form-item>
            <el-form-item label="密码">
              <el-input v-model="smtpConfig.password" type="password" show-password />
            </el-form-item>
            <el-form-item label="发件人地址">
              <el-input v-model="smtpConfig.from_address" placeholder="nvr@example.com" />
            </el-form-item>
            <el-form-item label="收件人">
              <el-tag
                v-for="(addr, i) in smtpConfig.to_addresses" :key="i"
                closable @close="smtpConfig.to_addresses.splice(i, 1)"
                style="margin-right: 6px; margin-bottom: 4px"
              >
                {{ addr }}
              </el-tag>
              <el-input
                v-model="newToAddress" size="small" style="width: 200px"
                placeholder="添加邮箱地址"
                @keyup.enter="addToAddress"
              />
            </el-form-item>
            <el-form-item>
              <el-button type="primary" @click="handleSaveNotifications" :loading="savingNotif">
                保存
              </el-button>
              <el-button @click="handleTestSmtp" :loading="testingSmtp">
                发送测试邮件
              </el-button>
            </el-form-item>
          </el-form>
        </el-card>
      </el-col>

      <!-- Webhook -->
      <el-col :span="12">
        <el-card>
          <template #header>
            <div style="display: flex; justify-content: space-between; align-items: center;">
              <span>Webhook 通知</span>
              <el-switch v-model="webhookConfig.enabled" />
            </div>
          </template>
          <el-form label-width="120px" :disabled="!webhookConfig.enabled">
            <el-form-item label="URL">
              <el-input v-model="webhookConfig.url" placeholder="https://example.com/webhook" />
            </el-form-item>
            <el-form-item label="签名密钥">
              <el-input v-model="webhookConfig.secret" placeholder="可选, HMAC-SHA256 签名" />
            </el-form-item>
            <el-form-item label="超时(秒)">
              <el-input-number v-model="webhookConfig.timeout_sec" :min="1" :max="60" />
            </el-form-item>
            <el-form-item label="重试次数">
              <el-input-number v-model="webhookConfig.retry_count" :min="0" :max="5" />
            </el-form-item>
            <el-form-item>
              <el-button type="primary" @click="handleSaveNotifications" :loading="savingNotif">
                保存
              </el-button>
              <el-button @click="handleTestWebhook" :loading="testingWebhook">
                发送测试
              </el-button>
            </el-form-item>
          </el-form>
        </el-card>
      </el-col>
    </el-row>

    <!-- MQTT & Cloud Backup -->
    <el-row :gutter="20" style="margin-top: 20px">
      <!-- MQTT -->
      <el-col :span="12">
        <el-card>
          <template #header>
            <div style="display: flex; justify-content: space-between; align-items: center;">
              <span>MQTT 集成</span>
              <el-switch v-model="mqttConfig.enabled" />
            </div>
          </template>
          <el-form label-width="120px" :disabled="!mqttConfig.enabled">
            <el-form-item label="Broker 地址">
              <el-input v-model="mqttConfig.broker_host" placeholder="mqtt.example.com" />
            </el-form-item>
            <el-form-item label="端口">
              <el-input-number v-model="mqttConfig.broker_port" :min="1" :max="65535" />
            </el-form-item>
            <el-form-item label="Client ID">
              <el-input v-model="mqttConfig.client_id" />
            </el-form-item>
            <el-form-item label="用户名">
              <el-input v-model="mqttConfig.username" />
            </el-form-item>
            <el-form-item label="密码">
              <el-input v-model="mqttConfig.password" type="password" show-password />
            </el-form-item>
            <el-form-item label="TLS">
              <el-switch v-model="mqttConfig.use_tls" />
            </el-form-item>
            <el-form-item label="事件 Topic">
              <el-input v-model="mqttConfig.event_topic" />
            </el-form-item>
            <el-form-item label="QoS">
              <el-select v-model="mqttConfig.qos" style="width: 100px">
                <el-option :value="0" label="0" />
                <el-option :value="1" label="1" />
                <el-option :value="2" label="2" />
              </el-select>
            </el-form-item>
            <el-form-item>
              <el-button type="primary" @click="handleSaveMqtt" :loading="savingMqtt">
                保存
              </el-button>
              <el-button @click="handleTestMqtt" :loading="testingMqtt">
                测试连接
              </el-button>
            </el-form-item>
          </el-form>
        </el-card>
      </el-col>

      <!-- Cloud Backup -->
      <el-col :span="12">
        <el-card>
          <template #header>
            <div style="display: flex; justify-content: space-between; align-items: center;">
              <span>云存储备份</span>
              <el-switch v-model="backupConfig.enabled" />
            </div>
          </template>
          <el-form label-width="120px" :disabled="!backupConfig.enabled">
            <el-form-item label="S3 端点">
              <el-input v-model="backupConfig.endpoint" placeholder="s3.amazonaws.com" />
            </el-form-item>
            <el-form-item label="区域">
              <el-input v-model="backupConfig.region" placeholder="us-east-1" />
            </el-form-item>
            <el-form-item label="存储桶">
              <el-input v-model="backupConfig.bucket" placeholder="my-nvr-backup" />
            </el-form-item>
            <el-form-item label="Access Key">
              <el-input v-model="backupConfig.access_key" />
            </el-form-item>
            <el-form-item label="Secret Key">
              <el-input v-model="backupConfig.secret_key" type="password" show-password />
            </el-form-item>
            <el-form-item label="使用 SSL">
              <el-switch v-model="backupConfig.use_ssl" />
            </el-form-item>
            <el-form-item label="前缀">
              <el-input v-model="backupConfig.prefix" />
            </el-form-item>
            <el-form-item label="备份模式">
              <el-select v-model="backupConfig.backup_mode">
                <el-option :value="0" label="仅事件录像" />
                <el-option :value="1" label="全量录像" />
                <el-option :value="2" label="自定义通道" />
              </el-select>
            </el-form-item>
            <el-form-item label="保留天数">
              <el-input-number v-model="backupConfig.retention_days" :min="0" :max="365" />
            </el-form-item>
            <el-form-item>
              <el-button type="primary" @click="handleSaveBackup" :loading="savingBackup">
                保存
              </el-button>
              <el-button @click="handleTestBackup" :loading="testingBackup">
                测试连接
              </el-button>
              <el-button @click="handleTriggerBackup">
                立即备份
              </el-button>
            </el-form-item>
          </el-form>
          <div v-if="backupStatus.running" style="margin-top: 12px">
            <el-descriptions :column="2" border size="small">
              <el-descriptions-item label="已上传">{{ backupStatus.total_uploaded }}</el-descriptions-item>
              <el-descriptions-item label="失败">{{ backupStatus.total_failed }}</el-descriptions-item>
            </el-descriptions>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <!-- Runtime Config (Hot Reload) -->
    <el-row style="margin-top: 20px">
      <el-col :span="24">
        <el-card>
          <template #header>
            <div style="display: flex; justify-content: space-between; align-items: center;">
              <span>运行时配置 (热更新)</span>
              <div>
                <el-button size="small" @click="loadRuntimeConfig" :loading="loadingConfig">
                  刷新
                </el-button>
                <el-button size="small" type="primary" @click="handleSaveConfig" :loading="savingConfig">
                  保存并应用
                </el-button>
              </div>
            </div>
          </template>
          <el-alert v-if="configChangeEvent" type="info" :closable="true" @close="configChangeEvent = ''" style="margin-bottom: 12px">
            配置已被外部更新 (变更的部分: {{ configChangeEvent }})
          </el-alert>
          <el-tabs v-model="configTab">
            <el-tab-pane label="AI 分析" name="ai">
              <el-form label-width="140px">
                <el-form-item label="默认推理后端">
                  <el-select v-model="runtimeConfig.ai.default_backend" style="width: 200px">
                    <el-option label="OpenCV DNN" value="opencv_dnn" />
                    <el-option label="ONNX Runtime" value="onnxruntime" />
                    <el-option label="TensorRT" value="tensorrt" />
                  </el-select>
                </el-form-item>
                <el-form-item label="默认模型">
                  <el-input v-model="runtimeConfig.ai.default_model" style="width: 200px" />
                </el-form-item>
                <el-form-item label="置信度阈值">
                  <el-slider v-model="runtimeConfig.ai.confidence_threshold"
                    :min="0.1" :max="0.95" :step="0.05"
                    :format-tooltip="v => `${(v * 100).toFixed(0)}%`"
                    style="width: 300px" />
                </el-form-item>
                <el-form-item label="分析帧率">
                  <el-input-number v-model="runtimeConfig.ai.analysis_fps" :min="1" :max="30" />
                  <span style="margin-left: 8px">FPS</span>
                </el-form-item>
                <el-form-item label="批量大小">
                  <el-input-number v-model="runtimeConfig.ai.batch_size" :min="1" :max="32" />
                </el-form-item>
              </el-form>
            </el-tab-pane>
            <el-tab-pane label="HLS 流媒体" name="hls">
              <el-form label-width="140px">
                <el-form-item label="切片时长">
                  <el-input-number v-model="runtimeConfig.hls.segment_duration_ms" :min="500" :max="10000" :step="500" />
                  <span style="margin-left: 8px">毫秒</span>
                </el-form-item>
                <el-form-item label="最大切片数">
                  <el-input-number v-model="runtimeConfig.hls.max_segments" :min="3" :max="20" />
                </el-form-item>
                <el-form-item label="最大切片大小">
                  <el-input-number v-model="runtimeConfig.hls.max_segment_size_kb" :min="512" :max="16384" :step="512" />
                  <span style="margin-left: 8px">KB</span>
                </el-form-item>
              </el-form>
            </el-tab-pane>
            <el-tab-pane label="Pipeline" name="pipeline">
              <el-form label-width="140px">
                <el-form-item label="队列容量">
                  <el-input-number v-model="runtimeConfig.pipeline.queue_capacity" :min="32" :max="1024" :step="32" />
                </el-form-item>
              </el-form>
            </el-tab-pane>
          </el-tabs>
        </el-card>
      </el-col>
    </el-row>

    <!-- Notification History -->
    <el-row style="margin-top: 20px">
      <el-col :span="24">
        <el-card>
          <template #header>通知发送记录</template>
          <el-table :data="notifHistory" stripe size="small" max-height="300">
            <el-table-column prop="channel_type" label="通道" width="80" />
            <el-table-column prop="title" label="标题" />
            <el-table-column prop="severity" label="级别" width="80" />
            <el-table-column label="状态" width="80">
              <template #default="{ row }">
                <el-tag :type="row.success ? 'success' : 'danger'" size="small">
                  {{ row.success ? '成功' : '失败' }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column prop="error" label="错误信息" />
            <el-table-column label="时间" width="180">
              <template #default="{ row }">
                {{ new Date(row.sent_at).toLocaleString() }}
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { useSystemStore } from '../stores/system'
import { systemApi } from '../api'
import axios from 'axios'

const systemStore = useSystemStore()
const systemStatus = computed(() => systemStore.status)
const saving = ref(false)
const savingNotif = ref(false)
const savingConfig = ref(false)
const loadingConfig = ref(false)
const testingSmtp = ref(false)
const testingWebhook = ref(false)
const savingMqtt = ref(false)
const testingMqtt = ref(false)
const savingBackup = ref(false)
const testingBackup = ref(false)
const newToAddress = ref('')
const notifHistory = ref([])
const configTab = ref('ai')
const configChangeEvent = ref('')

const runtimeConfig = reactive({
  ai: {
    default_backend: 'opencv_dnn',
    default_model: 'yolov8n',
    confidence_threshold: 0.5,
    analysis_fps: 5,
    batch_size: 8,
  },
  hls: {
    segment_duration_ms: 2000,
    max_segments: 5,
    max_segment_size_kb: 4096,
  },
  pipeline: {
    queue_capacity: 128,
  },
})

const storageConfig = reactive({
  path: '/recordings',
  segment_minutes: 30,
  retention_days: 30,
  max_size_gb: 100,
})

const smtpConfig = reactive({
  enabled: false,
  host: '',
  port: 587,
  use_tls: true,
  username: '',
  password: '',
  from_address: '',
  from_name: 'Loong AI NVR',
  to_addresses: [],
})

const mqttConfig = reactive({
  enabled: false,
  broker_host: 'localhost',
  broker_port: 1883,
  client_id: 'loong-nvr',
  username: '',
  password: '',
  use_tls: false,
  event_topic: 'loong/events',
  qos: 1,
})

const backupConfig = reactive({
  enabled: false,
  endpoint: '',
  region: 'us-east-1',
  bucket: '',
  access_key: '',
  secret_key: '',
  use_ssl: true,
  prefix: 'loong-nvr/',
  backup_mode: 0,
  retention_days: 30,
})

const backupStatus = reactive({
  running: false,
  total_uploaded: 0,
  total_failed: 0,
})

const webhookConfig = reactive({
  enabled: false,
  url: '',
  secret: '',
  timeout_sec: 10,
  retry_count: 2,
})

function addToAddress() {
  const addr = newToAddress.value.trim()
  if (addr && !smtpConfig.to_addresses.includes(addr)) {
    smtpConfig.to_addresses.push(addr)
    newToAddress.value = ''
  }
}

function getAuthHeaders() {
  const token = localStorage.getItem('token')
  return token ? { Authorization: `Bearer ${token}` } : {}
}

async function handleSaveStorage() {
  saving.value = true
  try {
    await systemApi.updateStorage({
      segment_minutes: storageConfig.segment_minutes,
      retention_days: storageConfig.retention_days,
      max_size_gb: storageConfig.max_size_gb,
    })
    ElMessage.success('存储设置已保存')
  } catch (err) {
    ElMessage.error('保存失败: ' + (err.response?.data?.error || err.message))
  } finally {
    saving.value = false
  }
}

async function handleSaveNotifications() {
  savingNotif.value = true
  try {
    await axios.put('/api/system/notifications', {
      smtp: { ...smtpConfig },
      webhook: { ...webhookConfig },
    }, { headers: getAuthHeaders() })
    ElMessage.success('通知设置已保存')
  } catch (err) {
    ElMessage.error('保存失败: ' + (err.response?.data?.error || err.message))
  } finally {
    savingNotif.value = false
  }
}

async function handleTestSmtp() {
  testingSmtp.value = true
  try {
    const { data } = await axios.post('/api/system/notifications/test',
      { channel: 'smtp' }, { headers: getAuthHeaders() })
    if (data.success) {
      ElMessage.success('测试邮件发送成功')
    } else {
      ElMessage.error('发送失败: ' + data.error)
    }
  } catch (err) {
    ElMessage.error('测试失败: ' + (err.response?.data?.error || err.message))
  } finally {
    testingSmtp.value = false
    loadNotifHistory()
  }
}

async function handleTestWebhook() {
  testingWebhook.value = true
  try {
    const { data } = await axios.post('/api/system/notifications/test',
      { channel: 'webhook' }, { headers: getAuthHeaders() })
    if (data.success) {
      ElMessage.success('Webhook 测试成功')
    } else {
      ElMessage.error('发送失败: ' + data.error)
    }
  } catch (err) {
    ElMessage.error('测试失败: ' + (err.response?.data?.error || err.message))
  } finally {
    testingWebhook.value = false
    loadNotifHistory()
  }
}

async function loadNotifConfig() {
  try {
    const { data } = await axios.get('/api/system/notifications',
      { headers: getAuthHeaders() })
    if (data.smtp) Object.assign(smtpConfig, data.smtp)
    if (data.webhook) Object.assign(webhookConfig, data.webhook)
  } catch {
    // Config may not exist yet
  }
}

async function loadNotifHistory() {
  try {
    const { data } = await axios.get('/api/system/notifications/history?limit=20',
      { headers: getAuthHeaders() })
    notifHistory.value = data
  } catch {
    // History may be empty
  }
}

async function loadRuntimeConfig() {
  loadingConfig.value = true
  try {
    const { data } = await systemApi.getConfig()
    if (data.ai) Object.assign(runtimeConfig.ai, data.ai)
    if (data.hls) Object.assign(runtimeConfig.hls, data.hls)
    if (data.pipeline) Object.assign(runtimeConfig.pipeline, data.pipeline)
  } catch {
    // May not be available for non-admin
  } finally {
    loadingConfig.value = false
  }
}

async function handleSaveConfig() {
  savingConfig.value = true
  try {
    await systemApi.updateConfig({
      ai: { ...runtimeConfig.ai },
      hls: { ...runtimeConfig.hls },
      pipeline: { ...runtimeConfig.pipeline },
    })
    ElMessage.success('配置已更新并生效')
  } catch (err) {
    ElMessage.error('更新失败: ' + (err.response?.data?.error || err.message))
  } finally {
    savingConfig.value = false
  }
}

// ---- MQTT ----
async function loadMqttConfig() {
  try {
    const { data } = await axios.get('/api/mqtt/config', { headers: getAuthHeaders() })
    Object.assign(mqttConfig, data)
  } catch { /* not configured yet */ }
}

async function handleSaveMqtt() {
  savingMqtt.value = true
  try {
    await axios.put('/api/mqtt/config', { ...mqttConfig }, { headers: getAuthHeaders() })
    ElMessage.success('MQTT 配置已保存')
  } catch (err) {
    ElMessage.error('保存失败: ' + (err.response?.data?.error || err.message))
  } finally {
    savingMqtt.value = false
  }
}

async function handleTestMqtt() {
  testingMqtt.value = true
  try {
    const { data } = await axios.post('/api/mqtt/test', {}, { headers: getAuthHeaders() })
    if (data.success) ElMessage.success('MQTT 连接测试成功')
    else ElMessage.error('MQTT 测试失败: ' + data.error_message)
  } catch (err) {
    ElMessage.error('测试失败: ' + (err.response?.data?.error || err.message))
  } finally {
    testingMqtt.value = false
  }
}

// ---- Cloud Backup ----
async function loadBackupConfig() {
  try {
    const { data } = await axios.get('/api/backup/config', { headers: getAuthHeaders() })
    Object.assign(backupConfig, data)
  } catch { /* not configured yet */ }
}

async function loadBackupStatus() {
  try {
    const { data } = await axios.get('/api/backup/status', { headers: getAuthHeaders() })
    Object.assign(backupStatus, data)
  } catch { /* not running */ }
}

async function handleSaveBackup() {
  savingBackup.value = true
  try {
    await axios.put('/api/backup/config', { ...backupConfig }, { headers: getAuthHeaders() })
    ElMessage.success('备份配置已保存')
  } catch (err) {
    ElMessage.error('保存失败: ' + (err.response?.data?.error || err.message))
  } finally {
    savingBackup.value = false
  }
}

async function handleTestBackup() {
  testingBackup.value = true
  try {
    const { data } = await axios.post('/api/backup/test', {}, { headers: getAuthHeaders() })
    if (data.success) ElMessage.success('云存储连接测试成功')
    else ElMessage.error('连接失败')
  } catch (err) {
    ElMessage.error('测试失败: ' + (err.response?.data?.error || err.message))
  } finally {
    testingBackup.value = false
  }
}

async function handleTriggerBackup() {
  try {
    await axios.post('/api/backup/trigger', {}, { headers: getAuthHeaders() })
    ElMessage.success('备份任务已触发')
  } catch (err) {
    ElMessage.error('触发失败: ' + (err.response?.data?.error || err.message))
  }
}

onMounted(() => {
  systemStore.fetchStatus()
  loadNotifConfig()
  loadNotifHistory()
  loadRuntimeConfig()
  loadMqttConfig()
  loadBackupConfig()
  loadBackupStatus()
})
</script>

<style scoped>
.settings-page {
  display: flex;
  flex-direction: column;
  gap: 20px;
}
</style>
