<template>
  <div class="channels-page">
    <el-card>
      <template #header>
        <div class="card-header">
          <span>通道管理</span>
          <el-button type="primary" @click="showAddDialog = true">
            <el-icon><Plus /></el-icon>
            添加通道
          </el-button>
        </div>
      </template>

      <el-table :data="channels" stripe v-loading="loading" empty-text="暂无通道，点击「添加通道」创建">
        <el-table-column prop="id" label="ID" width="80" />
        <el-table-column prop="name" label="名称" min-width="120" />
        <el-table-column prop="state" label="状态" width="120">
          <template #default="{ row }">
            <el-tag :type="stateType(row.state)" effect="dark" size="small">
              {{ row.state }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="frames_processed" label="处理帧数" width="120" />
        <el-table-column prop="frames_dropped" label="丢弃帧数" width="120" />
        <el-table-column label="操作" width="340" fixed="right">
          <template #default="{ row }">
            <el-button-group size="small">
              <el-button
                v-if="row.state !== 'Running'"
                type="success" @click="handleStart(row.id)"
              >启动</el-button>
              <el-button
                v-else
                type="warning" @click="handleStop(row.id)"
              >停止</el-button>
              <el-button type="primary" @click="handleEdit(row)">编辑</el-button>
              <el-button type="info" @click="handleOverlay(row)">叠加设置</el-button>
              <el-popconfirm
                title="确认删除该通道？"
                @confirm="handleDelete(row.id)"
              >
                <template #reference>
                  <el-button type="danger">删除</el-button>
                </template>
              </el-popconfirm>
            </el-button-group>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <!-- Add/Edit Dialog -->
    <el-dialog
      v-model="showAddDialog"
      :title="editingId ? '编辑通道' : '添加通道'"
      width="500px"
    >
      <el-form :model="form" label-width="100px">
        <el-form-item label="通道名称">
          <el-input v-model="form.name" placeholder="例: 前门摄像机" />
        </el-form-item>
        <el-form-item label="RTSP 地址">
          <el-input v-model="form.rtsp_url" placeholder="rtsp://..." />
        </el-form-item>
        <el-form-item label="分辨率">
          <el-row :gutter="8">
            <el-col :span="12">
              <el-input-number v-model="form.width" :min="320" :max="3840" />
            </el-col>
            <el-col :span="12">
              <el-input-number v-model="form.height" :min="240" :max="2160" />
            </el-col>
          </el-row>
        </el-form-item>
        <el-form-item label="帧率">
          <el-input-number v-model="form.framerate" :min="1" :max="60" />
        </el-form-item>
        <el-form-item label="AI 模型">
          <el-select v-model="form.ai_model" placeholder="选择模型">
            <el-option label="YOLOv5n" value="yolov5n" />
            <el-option label="YOLOv5s" value="yolov5s" />
            <el-option label="YOLOv8n" value="yolov8n" />
            <el-option label="YOLOv8s" value="yolov8s" />
            <el-option label="YOLOv11n" value="yolov11" />
            <el-option label="无 (仅录像)" value="" />
          </el-select>
        </el-form-item>
        <el-form-item label="GPS 位置">
          <el-row :gutter="8">
            <el-col :span="12">
              <el-input-number
                v-model="form.latitude"
                :precision="6" :step="0.001"
                :min="-90" :max="90"
                placeholder="纬度"
                controls-position="right"
                style="width: 100%"
              />
            </el-col>
            <el-col :span="12">
              <el-input-number
                v-model="form.longitude"
                :precision="6" :step="0.001"
                :min="-180" :max="180"
                placeholder="经度"
                controls-position="right"
                style="width: 100%"
              />
            </el-col>
          </el-row>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showAddDialog = false">取消</el-button>
        <el-button type="primary" @click="handleSubmit">确定</el-button>
      </template>
    </el-dialog>

    <!-- Overlay Settings Dialog -->
    <el-dialog
      v-model="showOverlayDialog"
      title="叠加渲染设置"
      width="560px"
    >
      <el-form :model="overlayForm" label-width="140px" v-loading="overlayLoading">
        <el-divider content-position="left">基础设置</el-divider>

        <el-form-item label="启用叠加渲染">
          <el-switch v-model="overlayForm.enabled" />
        </el-form-item>
        <el-form-item label="线条粗细">
          <el-slider v-model="overlayForm.line_thickness" :min="1" :max="8" :step="1" show-stops />
        </el-form-item>
        <el-form-item label="字体大小">
          <el-slider v-model="overlayForm.font_scale" :min="0.3" :max="2.0" :step="0.1" />
        </el-form-item>
        <el-form-item label="填充透明度">
          <el-slider v-model="overlayForm.fill_opacity" :min="0" :max="0.5" :step="0.01" />
        </el-form-item>

        <el-divider content-position="left">标签与置信度</el-divider>

        <el-form-item label="显示标签">
          <el-switch v-model="overlayForm.show_labels" />
        </el-form-item>
        <el-form-item label="显示置信度">
          <el-switch v-model="overlayForm.show_confidence" />
        </el-form-item>

        <el-divider content-position="left">OSD 信息</el-divider>

        <el-form-item label="显示时间戳">
          <el-switch v-model="overlayForm.show_timestamp" />
        </el-form-item>
        <el-form-item label="时间戳位置" v-if="overlayForm.show_timestamp">
          <el-select v-model="overlayForm.timestamp_position">
            <el-option label="左上角" :value="0" />
            <el-option label="右上角" :value="1" />
            <el-option label="左下角" :value="2" />
            <el-option label="右下角" :value="3" />
          </el-select>
        </el-form-item>
        <el-form-item label="时间格式" v-if="overlayForm.show_timestamp">
          <el-input
            v-model="overlayForm.timestamp_format"
            placeholder="默认: %Y-%m-%d %H:%M:%S"
          />
        </el-form-item>
        <el-form-item label="显示通道名称">
          <el-switch v-model="overlayForm.show_channel_name" />
        </el-form-item>

        <el-divider content-position="left">轨迹追踪</el-divider>

        <el-form-item label="显示运动轨迹">
          <el-switch v-model="overlayForm.show_trajectory" />
        </el-form-item>
        <el-form-item label="轨迹历史点数" v-if="overlayForm.show_trajectory">
          <el-input-number v-model="overlayForm.trajectory_max_points" :min="5" :max="100" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showOverlayDialog = false">取消</el-button>
        <el-button type="primary" @click="handleOverlaySubmit" :loading="overlayLoading">保存</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, reactive } from 'vue'
import { useRoute } from 'vue-router'
import { ElMessage } from 'element-plus'
import { useChannelStore } from '../stores/channel'
import { channelApi } from '../api'

const route = useRoute()

const channelStore = useChannelStore()
const channels = computed(() => channelStore.channels)
const loading = computed(() => channelStore.loading)

const showAddDialog = ref(false)
const editingId = ref(null)

const form = reactive({
  name: '',
  rtsp_url: '',
  width: 1920,
  height: 1080,
  framerate: 25,
  ai_model: 'yolov8n',
  latitude: 0,
  longitude: 0,
})

// Overlay settings
const showOverlayDialog = ref(false)
const overlayLoading = ref(false)
const overlayChannelId = ref(null)

const overlayForm = reactive({
  enabled: true,
  line_thickness: 2,
  font_scale: 0.5,
  fill_opacity: 0.08,
  show_labels: true,
  show_confidence: true,
  show_timestamp: true,
  show_channel_name: true,
  show_trajectory: false,
  trajectory_max_points: 30,
  timestamp_position: 0,
  timestamp_format: '',
})

function stateType(state) {
  return { Running: 'success', Stopped: 'info', Error: 'danger' }[state] || 'warning'
}

function resetForm() {
  form.name = ''
  form.rtsp_url = ''
  form.width = 1920
  form.height = 1080
  form.framerate = 25
  form.ai_model = 'yolov8n'
  form.latitude = 0
  form.longitude = 0
  editingId.value = null
}

function handleEdit(row) {
  editingId.value = row.id
  form.name = row.name || ''
  form.rtsp_url = row.rtsp_url || ''
  form.width = row.width || 1920
  form.height = row.height || 1080
  form.framerate = row.framerate || 25
  form.ai_model = row.ai_model || 'yolov8n'
  form.latitude = row.latitude || 0
  form.longitude = row.longitude || 0
  showAddDialog.value = true
}

async function handleSubmit() {
  if (!form.name || !form.rtsp_url) {
    ElMessage.warning('请填写名称和 RTSP 地址')
    return
  }
  try {
    if (editingId.value) {
      await channelStore.updateChannel(editingId.value, { ...form })
      ElMessage.success('通道更新成功')
    } else {
      await channelStore.createChannel({ ...form })
      ElMessage.success('通道创建成功')
    }
    showAddDialog.value = false
    resetForm()
  } catch (err) {
    ElMessage.error('操作失败')
  }
}

async function handleStart(id) {
  try {
    await channelStore.startChannel(id)
    ElMessage.success('通道已启动')
  } catch {
    ElMessage.error('启动失败')
  }
}

async function handleStop(id) {
  try {
    await channelStore.stopChannel(id)
    ElMessage.success('通道已停止')
  } catch {
    ElMessage.error('停止失败')
  }
}

async function handleDelete(id) {
  try {
    await channelStore.deleteChannel(id)
    ElMessage.success('通道已删除')
  } catch {
    ElMessage.error('删除失败')
  }
}

async function handleOverlay(row) {
  overlayChannelId.value = row.id
  overlayLoading.value = true
  showOverlayDialog.value = true

  try {
    const { data } = await channelApi.getOverlay(row.id)
    Object.assign(overlayForm, {
      enabled: data.enabled ?? true,
      line_thickness: data.line_thickness ?? 2,
      font_scale: data.font_scale ?? 0.5,
      fill_opacity: data.fill_opacity ?? 0.08,
      show_labels: data.show_labels ?? true,
      show_confidence: data.show_confidence ?? true,
      show_timestamp: data.show_timestamp ?? true,
      show_channel_name: data.show_channel_name ?? true,
      show_trajectory: data.show_trajectory ?? false,
      trajectory_max_points: data.trajectory_max_points ?? 30,
      timestamp_position: data.timestamp_position ?? 0,
      timestamp_format: data.timestamp_format ?? '',
    })
  } catch (err) {
    console.error('Failed to load overlay config:', err)
  } finally {
    overlayLoading.value = false
  }
}

async function handleOverlaySubmit() {
  overlayLoading.value = true
  try {
    await channelApi.updateOverlay(overlayChannelId.value, { ...overlayForm })
    ElMessage.success('叠加设置已保存（重启通道后生效）')
    showOverlayDialog.value = false
  } catch (err) {
    ElMessage.error('保存失败')
  } finally {
    overlayLoading.value = false
  }
}

onMounted(() => {
  channelStore.fetchChannels()
  if (route.query.add === '1') {
    resetForm()
    form.name = route.query.name || ''
    form.rtsp_url = route.query.rtsp_url || ''
    showAddDialog.value = true
  }
})
</script>

<style scoped>
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}
</style>
