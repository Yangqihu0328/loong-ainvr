<template>
  <div class="rules-container">
    <div class="page-header">
      <h2>智能分析规则</h2>
      <el-button type="primary" @click="openCreate">
        <el-icon><Plus /></el-icon> 新建规则
      </el-button>
    </div>

    <!-- Filters -->
    <div class="filter-bar">
      <el-select v-model="filterChannel" placeholder="全部通道" clearable style="width: 160px" @change="loadRules">
        <el-option v-for="ch in channels" :key="ch.id" :label="ch.name || `CH${ch.id}`" :value="ch.id" />
      </el-select>
      <el-select v-model="filterType" placeholder="全部类型" clearable style="width: 160px" @change="loadRules">
        <el-option label="越线检测" value="cross_line" />
        <el-option label="区域入侵" value="region_intrusion" />
        <el-option label="目标计数" value="object_counting" />
        <el-option label="徘徊检测" value="loitering" />
      </el-select>
    </div>

    <!-- Rules Table -->
    <el-table :data="rules" v-loading="loading" border stripe style="width: 100%">
      <el-table-column prop="id" label="ID" width="60" />
      <el-table-column prop="name" label="规则名称" min-width="140" />
      <el-table-column label="类型" width="120">
        <template #default="{ row }">
          <el-tag :type="typeTagMap[row.type]?.color || 'info'" size="small">
            {{ typeTagMap[row.type]?.label || row.type }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column prop="channel_id" label="通道" width="80" />
      <el-table-column label="状态" width="80">
        <template #default="{ row }">
          <el-switch v-model="row.enabled" @change="toggleRule(row)" size="small" />
        </template>
      </el-table-column>
      <el-table-column label="目标类别" min-width="120">
        <template #default="{ row }">
          {{ row.target_classes?.length ? row.target_classes.join(', ') : '全部' }}
        </template>
      </el-table-column>
      <el-table-column label="操作" width="160" fixed="right">
        <template #default="{ row }">
          <el-button size="small" @click="openEdit(row)">编辑</el-button>
          <el-popconfirm title="确认删除此规则？" @confirm="deleteRule(row.id)">
            <template #reference>
              <el-button size="small" type="danger">删除</el-button>
            </template>
          </el-popconfirm>
        </template>
      </el-table-column>
    </el-table>

    <!-- Counting Stats -->
    <div v-if="countingStats.length" class="counting-section">
      <h3>实时计数统计</h3>
      <el-row :gutter="16">
        <el-col :xs="12" :sm="8" :md="6" v-for="stat in countingStats" :key="stat.rule_id">
          <el-card shadow="hover" class="counting-card">
            <div class="counting-name">{{ stat.rule_name }}</div>
            <div class="counting-channel">CH{{ stat.channel_id }}</div>
            <div class="counting-numbers">
              <div class="count-item">
                <span class="count-label">A→B</span>
                <span class="count-value">{{ stat.count_a_to_b }}</span>
              </div>
              <div class="count-item">
                <span class="count-label">B→A</span>
                <span class="count-value">{{ stat.count_b_to_a }}</span>
              </div>
              <div class="count-item net">
                <span class="count-label">净计数</span>
                <span class="count-value">{{ stat.net_count }}</span>
              </div>
            </div>
          </el-card>
        </el-col>
      </el-row>
    </div>

    <!-- Create/Edit Dialog -->
    <el-dialog v-model="showDialog" :title="editingId ? '编辑规则' : '新建规则'" width="700px" destroy-on-close>
      <el-form :model="form" label-width="120px">
        <el-form-item label="规则名称" required>
          <el-input v-model="form.name" placeholder="例如: 大门越线检测" />
        </el-form-item>
        <el-form-item label="规则类型" required>
          <el-select v-model="form.type" style="width: 100%" @change="onTypeChange">
            <el-option label="越线检测" value="cross_line" />
            <el-option label="区域入侵" value="region_intrusion" />
            <el-option label="目标计数" value="object_counting" />
            <el-option label="徘徊检测" value="loitering" />
          </el-select>
        </el-form-item>
        <el-form-item label="关联通道" required>
          <el-select v-model="form.channel_id" style="width: 100%">
            <el-option v-for="ch in channels" :key="ch.id" :label="ch.name || `CH${ch.id}`" :value="ch.id" />
          </el-select>
        </el-form-item>
        <el-form-item label="启用">
          <el-switch v-model="form.enabled" />
        </el-form-item>

        <el-divider>检测参数</el-divider>

        <el-form-item label="目标类别">
          <el-select v-model="form.target_classes" multiple filterable allow-create placeholder="留空=全部类别" style="width: 100%">
            <el-option v-for="cls in commonClasses" :key="cls" :label="cls" :value="cls" />
          </el-select>
        </el-form-item>
        <el-form-item label="最低置信度">
          <el-slider v-model="form.min_confidence" :min="0.1" :max="1.0" :step="0.05" :format-tooltip="v => `${(v*100).toFixed(0)}%`" />
        </el-form-item>
        <el-form-item label="冷却时间(秒)">
          <el-input-number v-model="form.cooldown_sec" :min="1" :max="3600" />
        </el-form-item>

        <!-- Line Editor (cross_line / object_counting) -->
        <template v-if="form.type === 'cross_line' || form.type === 'object_counting'">
          <el-divider>虚拟线配置</el-divider>
          <el-form-item label="双向检测">
            <el-switch v-model="form.line.bidirectional" />
          </el-form-item>
          <div class="geometry-editor">
            <canvas ref="lineCanvas" width="480" height="270"
              @mousedown="onLineCanvasDown" @mousemove="onLineCanvasMove" @mouseup="onLineCanvasUp"
              class="editor-canvas" />
            <div class="editor-hint">点击拖拽设置虚拟线的起点和终点</div>
          </div>
        </template>

        <!-- Region Editor (region_intrusion / loitering) -->
        <template v-if="form.type === 'region_intrusion' || form.type === 'loitering'">
          <el-divider>区域配置</el-divider>
          <template v-if="form.type === 'loitering'">
            <el-form-item label="徘徊阈值(秒)">
              <el-input-number v-model="form.loiter_time_sec" :min="5" :max="3600" />
            </el-form-item>
          </template>
          <div class="geometry-editor">
            <canvas ref="regionCanvas" width="480" height="270"
              @click="onRegionCanvasClick" @mousemove="onRegionCanvasMove"
              @contextmenu.prevent="finishRegion"
              class="editor-canvas" />
            <div class="editor-hint">
              左键添加顶点，右键完成多边形
              <el-button size="small" type="warning" @click="clearRegion" style="margin-left: 12px">清除</el-button>
            </div>
          </div>
        </template>
      </el-form>

      <template #footer>
        <el-button @click="showDialog = false">取消</el-button>
        <el-button type="primary" @click="saveRule" :loading="saving">保存</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, nextTick, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { Plus } from '@element-plus/icons-vue'
import { rulesApi, channelApi, analyticsApi } from '../api'

const rules = ref([])
const channels = ref([])
const countingStats = ref([])
const loading = ref(false)
const saving = ref(false)
const showDialog = ref(false)
const editingId = ref(null)
const filterChannel = ref(null)
const filterType = ref(null)

const lineCanvas = ref(null)
const regionCanvas = ref(null)

const commonClasses = ['person', 'car', 'truck', 'bus', 'motorcycle', 'bicycle', 'dog', 'cat']

const typeTagMap = {
  cross_line: { label: '越线检测', color: 'warning' },
  region_intrusion: { label: '区域入侵', color: 'danger' },
  object_counting: { label: '目标计数', color: 'success' },
  loitering: { label: '徘徊检测', color: '' },
}

const defaultForm = () => ({
  name: '',
  type: 'cross_line',
  channel_id: null,
  enabled: true,
  target_classes: [],
  min_confidence: 0.5,
  cooldown_sec: 60,
  loiter_time_sec: 30,
  line: { start: { x: 0.2, y: 0.5 }, end: { x: 0.8, y: 0.5 }, bidirectional: true },
  region: { vertices: [] },
})

const form = reactive(defaultForm())

// ---- Line Editor State ----
let lineDragging = null  // 'start' | 'end' | null

function drawLineCanvas() {
  const canvas = lineCanvas.value
  if (!canvas) return
  const ctx = canvas.getContext('2d')
  const w = canvas.width, h = canvas.height

  ctx.clearRect(0, 0, w, h)
  ctx.fillStyle = '#1a1a2e'
  ctx.fillRect(0, 0, w, h)

  const sx = form.line.start.x * w, sy = form.line.start.y * h
  const ex = form.line.end.x * w, ey = form.line.end.y * h

  // Line
  ctx.strokeStyle = '#ffff00'
  ctx.lineWidth = 2
  ctx.beginPath()
  ctx.moveTo(sx, sy)
  ctx.lineTo(ex, ey)
  ctx.stroke()

  // Direction arrow
  const mx = (sx + ex) / 2, my = (sy + ey) / 2
  const dx = ex - sx, dy = ey - sy
  const len = Math.sqrt(dx * dx + dy * dy)
  if (len > 0) {
    const nx = -dy / len * 15, ny = dx / len * 15
    ctx.strokeStyle = '#00ff00'
    ctx.beginPath()
    ctx.moveTo(mx - nx, my - ny)
    ctx.lineTo(mx + nx, my + ny)
    ctx.stroke()
    // Arrow head
    const ax = mx + nx, ay = my + ny
    const adx = -nx / 3, ady = -ny / 3
    const px = dx / len * 5, py = dy / len * 5
    ctx.beginPath()
    ctx.moveTo(ax, ay)
    ctx.lineTo(ax + adx + px, ay + ady + py)
    ctx.moveTo(ax, ay)
    ctx.lineTo(ax + adx - px, ay + ady - py)
    ctx.stroke()
  }

  // Endpoints
  for (const [px, py, label] of [[sx, sy, 'A'], [ex, ey, 'B']]) {
    ctx.fillStyle = '#ff4444'
    ctx.beginPath()
    ctx.arc(px, py, 6, 0, Math.PI * 2)
    ctx.fill()
    ctx.fillStyle = '#fff'
    ctx.font = '12px sans-serif'
    ctx.fillText(label, px + 8, py - 4)
  }
}

function onLineCanvasDown(e) {
  const rect = lineCanvas.value.getBoundingClientRect()
  const x = (e.clientX - rect.left) / rect.width
  const y = (e.clientY - rect.top) / rect.height
  const ds = Math.hypot(x - form.line.start.x, y - form.line.start.y)
  const de = Math.hypot(x - form.line.end.x, y - form.line.end.y)
  lineDragging = ds < de ? 'start' : 'end'
  updateLinePoint(e)
}

function onLineCanvasMove(e) {
  if (!lineDragging) return
  updateLinePoint(e)
}

function onLineCanvasUp() {
  lineDragging = null
}

function updateLinePoint(e) {
  const rect = lineCanvas.value.getBoundingClientRect()
  const x = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width))
  const y = Math.max(0, Math.min(1, (e.clientY - rect.top) / rect.height))
  if (lineDragging === 'start') {
    form.line.start.x = x; form.line.start.y = y
  } else {
    form.line.end.x = x; form.line.end.y = y
  }
  drawLineCanvas()
}

// ---- Region Editor State ----
let regionTemp = null

function drawRegionCanvas() {
  const canvas = regionCanvas.value
  if (!canvas) return
  const ctx = canvas.getContext('2d')
  const w = canvas.width, h = canvas.height

  ctx.clearRect(0, 0, w, h)
  ctx.fillStyle = '#1a1a2e'
  ctx.fillRect(0, 0, w, h)

  const verts = form.region.vertices
  if (verts.length === 0) return

  // Filled polygon
  if (verts.length >= 3) {
    ctx.fillStyle = 'rgba(0, 200, 200, 0.15)'
    ctx.beginPath()
    ctx.moveTo(verts[0].x * w, verts[0].y * h)
    for (let i = 1; i < verts.length; i++) {
      ctx.lineTo(verts[i].x * w, verts[i].y * h)
    }
    ctx.closePath()
    ctx.fill()
  }

  // Border
  ctx.strokeStyle = '#00ffff'
  ctx.lineWidth = 2
  ctx.beginPath()
  ctx.moveTo(verts[0].x * w, verts[0].y * h)
  for (let i = 1; i < verts.length; i++) {
    ctx.lineTo(verts[i].x * w, verts[i].y * h)
  }
  if (verts.length >= 3) ctx.closePath()
  ctx.stroke()

  // Preview line to cursor
  if (regionTemp && verts.length > 0) {
    ctx.strokeStyle = 'rgba(0, 255, 255, 0.5)'
    ctx.setLineDash([5, 5])
    ctx.beginPath()
    ctx.moveTo(verts[verts.length - 1].x * w, verts[verts.length - 1].y * h)
    ctx.lineTo(regionTemp.x * w, regionTemp.y * h)
    ctx.stroke()
    ctx.setLineDash([])
  }

  // Vertices
  for (let i = 0; i < verts.length; i++) {
    ctx.fillStyle = i === 0 ? '#00ff00' : '#ff4444'
    ctx.beginPath()
    ctx.arc(verts[i].x * w, verts[i].y * h, 5, 0, Math.PI * 2)
    ctx.fill()
    ctx.fillStyle = '#fff'
    ctx.font = '11px sans-serif'
    ctx.fillText(`${i + 1}`, verts[i].x * w + 7, verts[i].y * h - 3)
  }
}

function onRegionCanvasClick(e) {
  const rect = regionCanvas.value.getBoundingClientRect()
  const x = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width))
  const y = Math.max(0, Math.min(1, (e.clientY - rect.top) / rect.height))
  form.region.vertices.push({ x, y })
  drawRegionCanvas()
}

function onRegionCanvasMove(e) {
  const rect = regionCanvas.value.getBoundingClientRect()
  regionTemp = {
    x: Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width)),
    y: Math.max(0, Math.min(1, (e.clientY - rect.top) / rect.height)),
  }
  drawRegionCanvas()
}

function finishRegion() {
  regionTemp = null
  drawRegionCanvas()
}

function clearRegion() {
  form.region.vertices = []
  regionTemp = null
  drawRegionCanvas()
}

// ---- Canvas redraw on dialog open ----
function onTypeChange() {
  nextTick(() => {
    if (form.type === 'cross_line' || form.type === 'object_counting') {
      drawLineCanvas()
    } else {
      drawRegionCanvas()
    }
  })
}

watch(showDialog, (val) => {
  if (val) {
    nextTick(() => onTypeChange())
  }
})

// ---- CRUD ----
async function loadRules() {
  loading.value = true
  try {
    const params = {}
    if (filterChannel.value != null) params.channel_id = filterChannel.value
    if (filterType.value) params.type = filterType.value
    const { data } = await rulesApi.list(params)
    rules.value = data.rules || []
  } catch {
    ElMessage.error('加载规则失败')
  } finally {
    loading.value = false
  }
}

async function loadChannels() {
  try {
    const { data } = await channelApi.list()
    channels.value = data.channels || data || []
  } catch { /* ignore */ }
}

async function loadCountingStats() {
  try {
    const { data } = await analyticsApi.counting()
    countingStats.value = data.counters || []
  } catch { /* ignore */ }
}

function openCreate() {
  editingId.value = null
  Object.assign(form, defaultForm())
  showDialog.value = true
}

function openEdit(row) {
  editingId.value = row.id
  Object.assign(form, {
    name: row.name,
    type: row.type,
    channel_id: row.channel_id,
    enabled: row.enabled,
    target_classes: row.target_classes || [],
    min_confidence: row.min_confidence || 0.5,
    cooldown_sec: row.cooldown_sec || 60,
    loiter_time_sec: row.loiter_time_sec || 30,
    line: row.line || { start: { x: 0.2, y: 0.5 }, end: { x: 0.8, y: 0.5 }, bidirectional: true },
    region: row.region || { vertices: [] },
  })
  showDialog.value = true
}

async function saveRule() {
  if (!form.name || !form.channel_id) {
    ElMessage.warning('请填写规则名称和关联通道')
    return
  }
  saving.value = true
  try {
    const payload = { ...form }
    if (editingId.value) {
      await rulesApi.update(editingId.value, payload)
      ElMessage.success('规则已更新')
    } else {
      await rulesApi.create(payload)
      ElMessage.success('规则已创建')
    }
    showDialog.value = false
    loadRules()
    loadCountingStats()
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '保存失败')
  } finally {
    saving.value = false
  }
}

async function toggleRule(row) {
  try {
    await rulesApi.update(row.id, { ...row })
  } catch {
    row.enabled = !row.enabled
    ElMessage.error('更新失败')
  }
}

async function deleteRule(id) {
  try {
    await rulesApi.delete(id)
    ElMessage.success('规则已删除')
    loadRules()
    loadCountingStats()
  } catch {
    ElMessage.error('删除失败')
  }
}

onMounted(() => {
  loadChannels()
  loadRules()
  loadCountingStats()
})
</script>

<style scoped>
.rules-container {
  padding: 20px;
}
.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}
.page-header h2 {
  margin: 0;
  font-size: 20px;
}
.filter-bar {
  display: flex;
  gap: 12px;
  margin-bottom: 16px;
  flex-wrap: wrap;
}
.counting-section {
  margin-top: 24px;
}
.counting-section h3 {
  margin-bottom: 12px;
  font-size: 16px;
}
.counting-card {
  text-align: center;
  margin-bottom: 12px;
}
.counting-name {
  font-weight: 600;
  font-size: 14px;
  margin-bottom: 4px;
}
.counting-channel {
  font-size: 12px;
  color: #999;
  margin-bottom: 8px;
}
.counting-numbers {
  display: flex;
  justify-content: space-around;
}
.count-item {
  display: flex;
  flex-direction: column;
  align-items: center;
}
.count-label {
  font-size: 11px;
  color: #888;
}
.count-value {
  font-size: 18px;
  font-weight: 700;
  color: #409eff;
}
.count-item.net .count-value {
  color: #67c23a;
}
.geometry-editor {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  margin: 12px 0;
}
.editor-canvas {
  border: 1px solid #444;
  border-radius: 4px;
  cursor: crosshair;
  max-width: 100%;
}
.editor-hint {
  font-size: 12px;
  color: #888;
}
</style>
