<template>
  <div class="recordings-page">
    <!-- Query Filters -->
    <el-card class="filter-card">
      <el-form :inline="!isMobile" class="filter-form" :label-position="isMobile ? 'top' : 'right'">
        <el-form-item label="通道">
          <el-select v-model="query.channel_id" placeholder="选择通道" clearable :style="{ width: isMobile ? '100%' : '180px' }">
            <el-option
              v-for="ch in channels"
              :key="ch.id"
              :label="`CH${ch.id} - ${ch.name}`"
              :value="ch.id"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="日期">
          <el-date-picker
            v-model="query.date"
            type="date"
            placeholder="选择日期"
            value-format="YYYY-MM-DD"
            :style="{ width: isMobile ? '100%' : '160px' }"
          />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" @click="loadTimeline" :loading="timelineLoading" :style="{ width: isMobile ? '100%' : 'auto' }">
            <el-icon><Search /></el-icon>
            加载时间轴
          </el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <el-row :gutter="16">
      <!-- Timeline + Player Column -->
      <el-col :xs="24" :sm="24" :md="16">
        <!-- Player Area -->
        <el-card class="player-card">
          <template #header>
            <div class="card-header">
              <span>录像回放</span>
              <div class="player-controls" v-if="currentSegment">
                <el-tag v-if="!isMobile" size="small" type="info">{{ currentSegment.codec }}</el-tag>
                <el-tag v-if="!isMobile" size="small">{{ currentSegment.resolution }}</el-tag>
                <el-divider v-if="!isMobile" direction="vertical" />
                <span class="control-label">倍速</span>
                <el-select v-model="playbackRate" size="small" style="width: 70px" @change="onRateChange">
                  <el-option v-for="r in playbackRates" :key="r" :label="`${r}x`" :value="r" />
                </el-select>
                <el-button size="small" @click="handleDownload(currentSegment)">
                  <el-icon><Download /></el-icon>
                  <span v-if="!isMobile">下载</span>
                </el-button>
              </div>
            </div>
          </template>
          <div class="playback-container">
            <video
              v-if="playbackUrl"
              ref="videoEl"
              :src="playbackUrl"
              controls
              autoplay
              playsinline
              @loadedmetadata="onVideoLoaded"
              @timeupdate="onTimeUpdate"
              @ended="onVideoEnded"
            />
            <div v-else class="no-playback">
              <el-icon :size="isMobile ? 36 : 48"><VideoPlay /></el-icon>
              <p>选择通道和日期后加载时间轴，点击片段进行播放</p>
            </div>
          </div>
        </el-card>

        <!-- Timeline Area -->
        <el-card class="timeline-card" v-if="timelineData">
          <template #header>
            <div class="card-header">
              <span>时间轴 ({{ timelineData.segments.length }} 片段)</span>
              <span class="timeline-date">{{ query.date || '今天' }}</span>
            </div>
          </template>
          <div
            class="timeline-wrapper"
            ref="timelineWrapperRef"
            @touchstart="onTimelineTouchStart"
            @touchmove="onTimelineTouchMove"
            @touchend="onTimelineTouchEnd"
          >
            <!-- Hour labels -->
            <div class="timeline-hours" :style="timelineInnerStyle">
              <span v-for="h in hourLabels" :key="h" class="hour-label" :style="{ left: `${(h / 24) * 100}%` }">
                {{ String(h).padStart(2, '0') }}
              </span>
            </div>
            <!-- Timeline bar -->
            <div class="timeline-bar" :style="timelineInnerStyle" ref="timelineBar" @click="onTimelineClick">
              <div
                v-for="seg in timelineData.segments"
                :key="seg.id"
                class="timeline-segment"
                :class="{ active: currentSegment?.id === seg.id, 'has-events': seg.event_count > 0 }"
                :style="getSegmentStyle(seg)"
                :title="`${formatTime(seg.start_time)} - ${formatTime(seg.end_time)}\n事件: ${seg.event_count}`"
                @click.stop="handlePlay(seg)"
              />
              <div
                v-for="evt in allTimelineEvents"
                :key="'e' + evt.id"
                class="timeline-event-marker"
                :style="{ left: getTimePosition(evt.event_time) + '%' }"
                :title="`${evt.event_type} @ ${formatTime(evt.event_time)}`"
                @click.stop="jumpToEvent(evt)"
              />
              <div
                v-if="playheadPosition >= 0"
                class="timeline-playhead"
                :style="{ left: playheadPosition + '%' }"
              />
            </div>
            <!-- Legend -->
            <div class="timeline-legend">
              <span class="legend-item"><span class="legend-color seg-color" /> 片段</span>
              <span class="legend-item"><span class="legend-color seg-active" /> 当前</span>
              <span class="legend-item"><span class="legend-color evt-color" /> 事件</span>
            </div>
          </div>
        </el-card>
      </el-col>

      <!-- Event Search + Segment List Column -->
      <el-col :xs="24" :sm="24" :md="8">
        <!-- Mobile: collapsible panels -->
        <el-collapse v-if="isMobile" v-model="mobilePanels">
          <el-collapse-item title="AI 事件搜索" name="events">
            <el-form :inline="false" size="small">
              <el-form-item label="事件类型">
                <el-select v-model="eventSearch.event_type" placeholder="全部" clearable style="width: 100%">
                  <el-option label="人员检测" value="person" />
                  <el-option label="车辆检测" value="vehicle" />
                  <el-option label="入侵检测" value="intrusion" />
                  <el-option label="遗留物检测" value="abandoned" />
                  <el-option label="徘徊检测" value="loitering" />
                  <el-option label="越线检测" value="line_cross" />
                </el-select>
              </el-form-item>
              <el-form-item label="最低置信度">
                <el-slider v-model="eventSearch.min_confidence" :min="0" :max="100" :step="5"
                           :format-tooltip="v => `${v}%`" />
              </el-form-item>
              <el-form-item>
                <el-button type="primary" @click="searchEvents" :loading="eventSearchLoading" style="width: 100%">
                  搜索事件
                </el-button>
              </el-form-item>
            </el-form>
            <div class="event-results" v-loading="eventSearchLoading">
              <div v-if="searchedEvents.length === 0" class="no-events">
                <el-empty description="暂无搜索结果" :image-size="60" />
              </div>
              <div v-for="evt in searchedEvents" :key="evt.id" class="event-item" @click="jumpToEvent(evt)">
                <div class="event-header">
                  <el-tag size="small" :type="getEventTagType(evt.event_type)">{{ getEventLabel(evt.event_type) }}</el-tag>
                  <span class="event-confidence">{{ (evt.confidence * 100).toFixed(0) }}%</span>
                </div>
                <div class="event-time">{{ formatTime(evt.event_time) }}</div>
              </div>
            </div>
          </el-collapse-item>
          <el-collapse-item title="录像片段列表" name="segments">
            <div class="segment-list" v-if="timelineData">
              <div v-for="seg in timelineData.segments" :key="seg.id" class="segment-item" :class="{ active: currentSegment?.id === seg.id }" @click="handlePlay(seg)">
                <div class="seg-time">{{ formatTimeShort(seg.start_time) }} - {{ formatTimeShort(seg.end_time) }}</div>
                <div class="seg-meta">
                  <el-tag size="small" type="info">{{ seg.codec }}</el-tag>
                  <span v-if="seg.event_count > 0" class="seg-events"><el-icon><Warning /></el-icon>{{ seg.event_count }}</span>
                </div>
              </div>
              <el-empty v-if="timelineData.segments.length === 0" description="暂无录像" :image-size="60" />
            </div>
            <el-empty v-else description="请先加载时间轴" :image-size="60" />
          </el-collapse-item>
        </el-collapse>

        <!-- Desktop: original card layout -->
        <template v-else>
          <el-card class="event-search-card">
            <template #header><span>AI 事件搜索</span></template>
            <el-form :inline="false" size="small">
              <el-form-item label="事件类型">
                <el-select v-model="eventSearch.event_type" placeholder="全部" clearable style="width: 100%">
                  <el-option label="人员检测" value="person" />
                  <el-option label="车辆检测" value="vehicle" />
                  <el-option label="入侵检测" value="intrusion" />
                  <el-option label="遗留物检测" value="abandoned" />
                  <el-option label="徘徊检测" value="loitering" />
                  <el-option label="越线检测" value="line_cross" />
                </el-select>
              </el-form-item>
              <el-form-item label="最低置信度">
                <el-slider v-model="eventSearch.min_confidence" :min="0" :max="100" :step="5"
                           :format-tooltip="v => `${v}%`" />
              </el-form-item>
              <el-form-item>
                <el-button type="primary" @click="searchEvents" :loading="eventSearchLoading" style="width: 100%">
                  <el-icon><Search /></el-icon>
                  搜索事件
                </el-button>
              </el-form-item>
            </el-form>
            <el-divider />
            <div class="event-results" v-loading="eventSearchLoading">
              <div v-if="searchedEvents.length === 0" class="no-events">
                <el-empty description="暂无搜索结果" :image-size="60" />
              </div>
              <div v-for="evt in searchedEvents" :key="evt.id" class="event-item" @click="jumpToEvent(evt)">
                <div class="event-header">
                  <el-tag size="small" :type="getEventTagType(evt.event_type)">{{ getEventLabel(evt.event_type) }}</el-tag>
                  <span class="event-confidence">{{ (evt.confidence * 100).toFixed(0) }}%</span>
                </div>
                <div class="event-time">{{ formatTime(evt.event_time) }}</div>
              </div>
            </div>
          </el-card>

          <el-card class="segment-list-card">
            <template #header><span>录像片段列表</span></template>
            <div class="segment-list" v-if="timelineData">
              <div v-for="seg in timelineData.segments" :key="seg.id" class="segment-item" :class="{ active: currentSegment?.id === seg.id }" @click="handlePlay(seg)">
                <div class="seg-time">{{ formatTimeShort(seg.start_time) }} - {{ formatTimeShort(seg.end_time) }}</div>
                <div class="seg-meta">
                  <el-tag size="small" type="info">{{ seg.codec }}</el-tag>
                  <span v-if="seg.event_count > 0" class="seg-events"><el-icon><Warning /></el-icon>{{ seg.event_count }}</span>
                  <el-button size="small" text type="info" @click.stop="handleDownload(seg)">
                    <el-icon><Download /></el-icon>
                  </el-button>
                </div>
              </div>
              <el-empty v-if="timelineData.segments.length === 0" description="该时间段暂无录像" :image-size="60" />
            </div>
            <el-empty v-else description="请先加载时间轴" :image-size="60" />
          </el-card>
        </template>
      </el-col>
    </el-row>
  </div>
</template>

<script setup>
import { ref, computed, reactive, onMounted, onUnmounted } from 'vue'
import { useRoute } from 'vue-router'
import { ElMessage } from 'element-plus'
import { useChannelStore } from '../stores/channel'
import { recordingApi } from '../api'

const route = useRoute()
const channelStore = useChannelStore()
const channels = computed(() => channelStore.channels)

const MOBILE_BREAKPOINT = 768
const isMobile = ref(window.innerWidth < MOBILE_BREAKPOINT)
function onResize() { isMobile.value = window.innerWidth < MOBILE_BREAKPOINT }

const mobilePanels = ref([])

const videoEl = ref(null)
const timelineBar = ref(null)
const timelineWrapperRef = ref(null)

const timelineLoading = ref(false)
const eventSearchLoading = ref(false)
const timelineData = ref(null)
const currentSegment = ref(null)
const playbackUrl = ref('')
const playheadPosition = ref(-1)
const searchedEvents = ref([])

const playbackRate = ref(1)
const playbackRates = [0.25, 0.5, 1, 1.5, 2, 4, 8]

const query = reactive({ channel_id: null, date: null })
const eventSearch = reactive({ event_type: '', min_confidence: 50 })

const allTimelineEvents = computed(() => {
  if (!timelineData.value) return []
  const events = []
  for (const seg of timelineData.value.segments) {
    if (seg.events) events.push(...seg.events)
  }
  return events
})

const dayStart = computed(() => {
  if (!query.date) return 0
  return new Date(query.date + 'T00:00:00').getTime()
})

const dayEnd = computed(() => dayStart.value + 24 * 60 * 60 * 1000)

const hourLabels = computed(() => {
  if (isMobile.value) return [0, 3, 6, 9, 12, 15, 18, 21, 24]
  const labels = []
  for (let h = 0; h <= 24; h++) labels.push(h)
  return labels
})

// Pinch-zoom state for mobile timeline
const timelineZoom = ref(1)
const timelineOffset = ref(0)
let pinchStartDist = 0
let pinchStartZoom = 1

const timelineInnerStyle = computed(() => {
  if (!isMobile.value || timelineZoom.value <= 1) return {}
  return {
    width: `${timelineZoom.value * 100}%`,
    transform: `translateX(-${timelineOffset.value}px)`,
  }
})

function onTimelineTouchStart(e) {
  if (!isMobile.value) return
  if (e.touches.length === 2) {
    const dx = e.touches[0].clientX - e.touches[1].clientX
    const dy = e.touches[0].clientY - e.touches[1].clientY
    pinchStartDist = Math.hypot(dx, dy)
    pinchStartZoom = timelineZoom.value
  }
}

function onTimelineTouchMove(e) {
  if (!isMobile.value || e.touches.length !== 2) return
  const dx = e.touches[0].clientX - e.touches[1].clientX
  const dy = e.touches[0].clientY - e.touches[1].clientY
  const dist = Math.hypot(dx, dy)
  if (pinchStartDist > 0) {
    const newZoom = Math.min(4, Math.max(1, pinchStartZoom * (dist / pinchStartDist)))
    timelineZoom.value = newZoom
    const wrapperW = timelineWrapperRef.value?.offsetWidth || 300
    const maxOffset = wrapperW * (newZoom - 1)
    timelineOffset.value = Math.min(timelineOffset.value, Math.max(0, maxOffset))
  }
}

function onTimelineTouchEnd() {
  pinchStartDist = 0
}

async function loadTimeline() {
  if (!query.channel_id) {
    ElMessage.warning('请先选择通道')
    return
  }
  if (!query.date) {
    const today = new Date()
    query.date = today.toISOString().split('T')[0]
  }
  timelineLoading.value = true
  try {
    const { data } = await recordingApi.timeline({
      channel_id: query.channel_id,
      start: dayStart.value,
      end: dayEnd.value,
    })
    timelineData.value = data
    currentSegment.value = null
    playbackUrl.value = ''
    playheadPosition.value = -1
  } catch {
    ElMessage.error('加载时间轴失败')
  } finally {
    timelineLoading.value = false
  }
}

async function searchEvents() {
  if (!query.channel_id) {
    ElMessage.warning('请先选择通道')
    return
  }
  eventSearchLoading.value = true
  try {
    const params = {
      channel_id: query.channel_id,
      start: dayStart.value || 0,
      end: dayEnd.value || Date.now(),
    }
    if (eventSearch.event_type) params.event_type = eventSearch.event_type
    if (eventSearch.min_confidence > 0) params.min_confidence = eventSearch.min_confidence / 100
    const { data } = await recordingApi.searchEvents(params)
    searchedEvents.value = data || []
    if (searchedEvents.value.length === 0) ElMessage.info('未找到匹配事件')
  } catch {
    ElMessage.error('搜索失败')
  } finally {
    eventSearchLoading.value = false
  }
}

function getSegmentStyle(seg) {
  const start = Math.max(seg.start_time, dayStart.value)
  const end = Math.min(seg.end_time, dayEnd.value)
  const dayDuration = dayEnd.value - dayStart.value
  const left = ((start - dayStart.value) / dayDuration) * 100
  const width = ((end - start) / dayDuration) * 100
  return { left: `${left}%`, width: `${Math.max(width, 0.15)}%` }
}

function getTimePosition(timestamp) {
  const dayDuration = dayEnd.value - dayStart.value
  return ((timestamp - dayStart.value) / dayDuration) * 100
}

function onTimelineClick(e) {
  if (!timelineBar.value || !timelineData.value) return
  const rect = timelineBar.value.getBoundingClientRect()
  const ratio = (e.clientX - rect.left) / rect.width
  const clickTime = dayStart.value + ratio * (dayEnd.value - dayStart.value)
  const seg = timelineData.value.segments.find(
    s => s.start_time <= clickTime && s.end_time >= clickTime
  )
  if (seg) handlePlay(seg, clickTime)
}

function handlePlay(seg, seekTime) {
  currentSegment.value = seg
  playbackUrl.value = `/api/recordings/playback?file=${encodeURIComponent(seg.file_path)}`
  playbackRate.value = 1
  if (seekTime && videoEl.value) {
    videoEl.value.currentTime = (seekTime - seg.start_time) / 1000
  }
}

function jumpToEvent(evt) {
  if (!timelineData.value) return
  const seg = timelineData.value.segments.find(
    s => s.start_time <= evt.event_time && s.end_time >= evt.event_time
  )
  if (seg) handlePlay(seg, evt.event_time)
  else ElMessage.info('未找到包含该事件的录像片段')
}

function onVideoLoaded() {
  if (videoEl.value) videoEl.value.playbackRate = playbackRate.value
}

function onTimeUpdate() {
  if (!videoEl.value || !currentSegment.value) return
  const currentTime = currentSegment.value.start_time + videoEl.value.currentTime * 1000
  playheadPosition.value = getTimePosition(currentTime)
}

function onVideoEnded() {
  if (!currentSegment.value || !timelineData.value) return
  const segments = timelineData.value.segments
  const idx = segments.findIndex(s => s.id === currentSegment.value.id)
  if (idx >= 0 && idx < segments.length - 1) handlePlay(segments[idx + 1])
}

function onRateChange(rate) {
  if (videoEl.value) videoEl.value.playbackRate = rate
}

function handleDownload(seg) {
  if (!seg?.file_path) {
    ElMessage.warning('录像文件不可用')
    return
  }
  const link = document.createElement('a')
  link.href = `/api/recordings/download?file=${encodeURIComponent(seg.file_path)}`
  link.download = `ch${seg.channel_id}_${formatTimeFile(seg.start_time)}.mp4`
  link.click()
}

function formatTime(ts) {
  if (!ts) return '-'
  return new Date(ts).toLocaleString('zh-CN')
}

function formatTimeShort(ts) {
  if (!ts) return '-'
  return new Date(ts).toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit', second: '2-digit' })
}

function formatTimeFile(ts) {
  const d = new Date(ts)
  return `${d.getFullYear()}${String(d.getMonth() + 1).padStart(2, '0')}${String(d.getDate()).padStart(2, '0')}_${String(d.getHours()).padStart(2, '0')}${String(d.getMinutes()).padStart(2, '0')}${String(d.getSeconds()).padStart(2, '0')}`
}

function getEventTagType(type) {
  const map = { person: '', vehicle: 'success', intrusion: 'danger', abandoned: 'warning', loitering: 'warning', line_cross: 'danger' }
  return map[type] || 'info'
}

function getEventLabel(type) {
  const map = { person: '人员', vehicle: '车辆', intrusion: '入侵', abandoned: '遗留物', loitering: '徘徊', line_cross: '越线' }
  return map[type] || type
}

onMounted(() => {
  channelStore.fetchChannels()
  window.addEventListener('resize', onResize)
  if (route.query.channel_id) query.channel_id = Number(route.query.channel_id)
  if (route.query.time) {
    const ts = Number(route.query.time)
    query.date = new Date(ts).toISOString().split('T')[0]
    loadTimeline()
  }
})

onUnmounted(() => {
  window.removeEventListener('resize', onResize)
})
</script>

<style scoped>
.recordings-page {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.filter-card {
  flex-shrink: 0;
}

.filter-form {
  margin-bottom: 0;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 8px;
}

.player-controls {
  display: flex;
  align-items: center;
  gap: 8px;
}

.control-label {
  font-size: 12px;
  color: #909399;
}

.player-card .playback-container {
  min-height: 260px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #000;
  border-radius: 4px;
  overflow: hidden;
}

.playback-container video {
  width: 100%;
  max-height: 480px;
  background: #000;
}

.no-playback {
  text-align: center;
  color: #909399;
  padding: 40px 16px;
}

.no-playback p {
  margin-top: 12px;
  font-size: 14px;
}

/* ---- Timeline ---- */
.timeline-card {
  margin-top: 16px;
}

.timeline-wrapper {
  position: relative;
  padding: 0 4px;
  overflow: hidden;
  touch-action: pan-y pinch-zoom;
}

.timeline-hours {
  position: relative;
  height: 20px;
  font-size: 10px;
  color: #909399;
}

.hour-label {
  position: absolute;
  transform: translateX(-50%);
}

.timeline-bar {
  position: relative;
  height: 36px;
  background: #f0f2f5;
  border-radius: 4px;
  cursor: pointer;
  overflow: visible;
}

.timeline-segment {
  position: absolute;
  top: 2px;
  height: 32px;
  background: #409eff;
  border-radius: 2px;
  opacity: 0.7;
  transition: opacity 0.2s;
  min-width: 2px;
  cursor: pointer;
}

.timeline-segment:hover { opacity: 1; }
.timeline-segment.active { background: #67c23a; opacity: 1; z-index: 2; }
.timeline-segment.has-events { background: #e6a23c; }
.timeline-segment.has-events.active { background: #67c23a; }

.timeline-event-marker {
  position: absolute;
  top: -4px;
  width: 8px;
  height: 8px;
  background: #f56c6c;
  border-radius: 50%;
  transform: translateX(-50%);
  z-index: 3;
  cursor: pointer;
  transition: transform 0.15s;
}

.timeline-event-marker:hover {
  transform: translateX(-50%) scale(1.5);
}

.timeline-playhead {
  position: absolute;
  top: -6px;
  width: 2px;
  height: 48px;
  background: #e6a23c;
  z-index: 4;
  transform: translateX(-1px);
  pointer-events: none;
}

.timeline-legend {
  display: flex;
  gap: 12px;
  margin-top: 8px;
  font-size: 11px;
  color: #909399;
}

.legend-item {
  display: flex;
  align-items: center;
  gap: 4px;
}

.legend-color {
  display: inline-block;
  width: 12px;
  height: 12px;
  border-radius: 2px;
}

.seg-color { background: #409eff; }
.seg-active { background: #67c23a; }
.evt-color { background: #f56c6c; border-radius: 50%; }

.timeline-date {
  font-size: 12px;
  color: #909399;
}

/* ---- Event Search ---- */
.event-search-card {
  margin-bottom: 16px;
}

.event-results {
  max-height: 300px;
  overflow-y: auto;
}

.no-events {
  padding: 10px 0;
}

.event-item {
  padding: 8px;
  border-bottom: 1px solid #f0f2f5;
  cursor: pointer;
  border-radius: 4px;
  transition: background 0.15s;
}

.event-item:hover { background: #f5f7fa; }
.event-item:last-child { border-bottom: none; }

.event-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.event-confidence {
  font-size: 12px;
  color: #909399;
  font-weight: 600;
}

.event-time {
  font-size: 11px;
  color: #c0c4cc;
  margin-top: 4px;
}

/* ---- Segment List ---- */
.segment-list {
  max-height: 400px;
  overflow-y: auto;
}

.segment-item {
  padding: 8px 10px;
  border-bottom: 1px solid #f0f2f5;
  cursor: pointer;
  border-radius: 4px;
  transition: background 0.15s;
}

.segment-item:hover { background: #f5f7fa; }
.segment-item.active { background: #ecf5ff; border-left: 3px solid #409eff; }
.segment-item:last-child { border-bottom: none; }

.seg-time {
  font-size: 13px;
  font-weight: 500;
  color: #303133;
}

.seg-meta {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-top: 4px;
}

.seg-events {
  display: flex;
  align-items: center;
  gap: 2px;
  font-size: 11px;
  color: #e6a23c;
}

@media (max-width: 768px) {
  .playback-container video {
    max-height: 300px;
  }
  .player-card .playback-container {
    min-height: 200px;
  }
  .timeline-bar {
    height: 44px;
  }
  .timeline-segment {
    top: 4px;
    height: 36px;
  }
  .timeline-event-marker {
    width: 10px;
    height: 10px;
  }
}
</style>
