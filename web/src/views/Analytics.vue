<template>
  <div class="analytics-page">
    <!-- Time range selector -->
    <el-card class="filter-card" shadow="never">
      <div class="filter-bar">
        <el-radio-group v-model="timePreset" size="small" @change="onPresetChange">
          <el-radio-button value="today">今天</el-radio-button>
          <el-radio-button value="week">本周</el-radio-button>
          <el-radio-button value="month">本月</el-radio-button>
          <el-radio-button value="custom">自定义</el-radio-button>
        </el-radio-group>
        <el-date-picker
          v-if="timePreset === 'custom'"
          v-model="customRange"
          type="datetimerange"
          range-separator="至"
          start-placeholder="开始"
          end-placeholder="结束"
          style="max-width: 340px"
          @change="fetchAll"
        />
        <el-select v-model="channelId" placeholder="全部通道" clearable size="small" style="width: 150px" @change="fetchAll">
          <el-option
            v-for="ch in channels"
            :key="ch.id"
            :label="`CH${ch.id} - ${ch.name}`"
            :value="ch.id"
          />
        </el-select>
        <el-button type="primary" :icon="Refresh" size="small" @click="fetchAll" :loading="loading">
          刷新
        </el-button>
      </div>
    </el-card>

    <!-- Summary cards -->
    <el-row :gutter="16" class="summary-row">
      <el-col :xs="12" :sm="6">
        <el-card shadow="hover" class="stat-card stat-total">
          <div class="stat-content">
            <div class="stat-icon"><el-icon :size="36"><Bell /></el-icon></div>
            <div class="stat-info">
              <div class="stat-value">{{ summary.total_events }}</div>
              <div class="stat-label">总事件数</div>
            </div>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="6">
        <el-card shadow="hover" class="stat-card stat-crossline">
          <div class="stat-content">
            <div class="stat-icon"><el-icon :size="36"><Position /></el-icon></div>
            <div class="stat-info">
              <div class="stat-value">{{ summary.cross_line_events }}</div>
              <div class="stat-label">越线事件</div>
            </div>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="6">
        <el-card shadow="hover" class="stat-card stat-counting">
          <div class="stat-content">
            <div class="stat-icon"><el-icon :size="36"><Odometer /></el-icon></div>
            <div class="stat-info">
              <div class="stat-value">{{ summary.total_count_value }}</div>
              <div class="stat-label">通行计数</div>
            </div>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="6">
        <el-card shadow="hover" class="stat-card stat-channels">
          <div class="stat-content">
            <div class="stat-icon"><el-icon :size="36"><VideoCamera /></el-icon></div>
            <div class="stat-info">
              <div class="stat-value">{{ summary.active_channels }}</div>
              <div class="stat-label">活跃通道</div>
            </div>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <!-- Charts row 1: Trend line + Event type distribution -->
    <el-row :gutter="16" class="chart-row">
      <el-col :xs="24" :lg="16">
        <el-card>
          <template #header>
            <div class="card-header">
              <span>事件趋势</span>
              <el-radio-group v-model="trendGranularity" size="small" @change="fetchTrends">
                <el-radio-button value="hourly">小时</el-radio-button>
                <el-radio-button value="daily">天</el-radio-button>
                <el-radio-button value="weekly">周</el-radio-button>
              </el-radio-group>
            </div>
          </template>
          <div ref="trendChartRef" class="chart-container"></div>
        </el-card>
      </el-col>
      <el-col :xs="24" :lg="8">
        <el-card>
          <template #header><span>事件类型分布</span></template>
          <div ref="pieChartRef" class="chart-container"></div>
        </el-card>
      </el-col>
    </el-row>

    <!-- Charts row 2: Peak hours + Heatmap -->
    <el-row :gutter="16" class="chart-row">
      <el-col :xs="24" :lg="12">
        <el-card>
          <template #header><span>24 小时事件分布</span></template>
          <div ref="peakChartRef" class="chart-container"></div>
        </el-card>
      </el-col>
      <el-col :xs="24" :lg="12">
        <el-card>
          <template #header><span>检测热力图</span></template>
          <AnalyticsHeatmap
            :cells="heatmapCells"
            :grid-cols="heatmapGridCols"
            :grid-rows="heatmapGridRows"
          />
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, nextTick, shallowRef } from 'vue'
import { Refresh } from '@element-plus/icons-vue'
import { ElMessage } from 'element-plus'
import * as echarts from 'echarts/core'
import { LineChart, PieChart, BarChart } from 'echarts/charts'
import {
  TitleComponent, TooltipComponent, LegendComponent,
  GridComponent, DataZoomComponent,
} from 'echarts/components'
import { CanvasRenderer } from 'echarts/renderers'
import { analyticsApi, channelApi } from '../api'
import AnalyticsHeatmap from '../components/AnalyticsHeatmap.vue'

echarts.use([
  LineChart, PieChart, BarChart,
  TitleComponent, TooltipComponent, LegendComponent,
  GridComponent, DataZoomComponent,
  CanvasRenderer,
])

const loading = ref(false)
const timePreset = ref('today')
const customRange = ref(null)
const channelId = ref(null)
const trendGranularity = ref('hourly')
const channels = ref([])

const summary = ref({
  total_events: 0, cross_line_events: 0, region_intrusion_events: 0,
  object_counting_events: 0, loitering_events: 0,
  total_count_value: 0, active_channels: 0,
})

const trendData = ref([])
const peakData = ref([])
const heatmapCells = ref([])
const heatmapGridCols = 20
const heatmapGridRows = 15

// Chart refs
const trendChartRef = ref(null)
const pieChartRef = ref(null)
const peakChartRef = ref(null)
const trendChart = shallowRef(null)
const pieChart = shallowRef(null)
const peakChart = shallowRef(null)

function getTimeRange() {
  const now = Date.now()
  if (timePreset.value === 'custom' && customRange.value) {
    return { start_time: customRange.value[0].getTime(), end_time: customRange.value[1].getTime() }
  }
  const dayMs = 86400000
  switch (timePreset.value) {
    case 'today': {
      const todayStart = new Date(); todayStart.setHours(0, 0, 0, 0)
      return { start_time: todayStart.getTime(), end_time: now }
    }
    case 'week':
      return { start_time: now - 7 * dayMs, end_time: now }
    case 'month':
      return { start_time: now - 30 * dayMs, end_time: now }
    default:
      return { start_time: now - dayMs, end_time: now }
  }
}

function buildParams() {
  const range = getTimeRange()
  const params = { ...range }
  if (channelId.value != null) params.channel_id = channelId.value
  return params
}

async function fetchSummary() {
  try {
    const { data } = await analyticsApi.summary(buildParams())
    summary.value = data
  } catch { /* empty */ }
}

async function fetchTrends() {
  try {
    const params = { ...buildParams(), granularity: trendGranularity.value }
    const { data } = await analyticsApi.trends(params)
    trendData.value = data.trends || []
    updateTrendChart()
  } catch { /* empty */ }
}

async function fetchPeakHours() {
  try {
    const { data } = await analyticsApi.peakHours(buildParams())
    peakData.value = data.peak_hours || []
    updatePeakChart()
  } catch { /* empty */ }
}

async function fetchHeatmap() {
  try {
    const params = {
      ...buildParams(),
      grid_cols: heatmapGridCols,
      grid_rows: heatmapGridRows,
    }
    const { data } = await analyticsApi.heatmap(params)
    heatmapCells.value = data.cells || []
  } catch { /* empty */ }
}

async function fetchAll() {
  loading.value = true
  try {
    await Promise.all([fetchSummary(), fetchTrends(), fetchPeakHours(), fetchHeatmap()])
    updatePieChart()
  } catch {
    ElMessage.error('加载分析数据失败')
  } finally {
    loading.value = false
  }
}

function onPresetChange() {
  if (timePreset.value !== 'custom') {
    customRange.value = null
    fetchAll()
  }
}

// ---- Chart rendering ----

function formatBucketTime(ts) {
  const d = new Date(ts)
  if (trendGranularity.value === 'hourly') {
    return `${d.getMonth() + 1}/${d.getDate()} ${String(d.getHours()).padStart(2, '0')}:00`
  }
  return `${d.getMonth() + 1}/${d.getDate()}`
}

function updateTrendChart() {
  if (!trendChart.value) return
  const xData = trendData.value.map(b => formatBucketTime(b.bucket_start))
  const yData = trendData.value.map(b => b.event_count)

  trendChart.value.setOption({
    tooltip: { trigger: 'axis' },
    grid: { left: 50, right: 20, top: 20, bottom: 60 },
    dataZoom: [{ type: 'inside' }, { type: 'slider', height: 20 }],
    xAxis: { type: 'category', data: xData, axisLabel: { rotate: 30, fontSize: 11 } },
    yAxis: { type: 'value', minInterval: 1 },
    series: [{
      name: '事件数', type: 'line', smooth: true, data: yData,
      areaStyle: { opacity: 0.15 },
      lineStyle: { width: 2 },
      itemStyle: { color: '#409eff' },
    }],
  }, true)
}

function updatePieChart() {
  if (!pieChart.value) return
  const items = [
    { name: '越线检测', value: summary.value.cross_line_events },
    { name: '区域入侵', value: summary.value.region_intrusion_events },
    { name: '目标计数', value: summary.value.object_counting_events },
    { name: '徘徊检测', value: summary.value.loitering_events },
  ].filter(i => i.value > 0)

  pieChart.value.setOption({
    tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
    legend: { orient: 'vertical', left: 'left', top: 'center' },
    color: ['#409eff', '#67c23a', '#e6a23c', '#f56c6c'],
    series: [{
      type: 'pie', radius: ['40%', '70%'],
      center: ['60%', '50%'],
      label: { show: true, formatter: '{b}\n{d}%' },
      data: items.length > 0 ? items : [{ name: '暂无数据', value: 0 }],
    }],
  }, true)
}

function updatePeakChart() {
  if (!peakChart.value) return
  const hours = Array.from({ length: 24 }, (_, i) => `${String(i).padStart(2, '0')}:00`)
  const counts = new Array(24).fill(0)
  for (const p of peakData.value) {
    if (p.hour >= 0 && p.hour < 24) counts[p.hour] = p.event_count
  }

  peakChart.value.setOption({
    tooltip: { trigger: 'axis' },
    grid: { left: 50, right: 20, top: 20, bottom: 40 },
    xAxis: { type: 'category', data: hours, axisLabel: { fontSize: 11 } },
    yAxis: { type: 'value', minInterval: 1 },
    series: [{
      type: 'bar', data: counts,
      itemStyle: {
        color: (params) => {
          const maxVal = Math.max(...counts, 1)
          const ratio = params.value / maxVal
          if (ratio > 0.7) return '#f56c6c'
          if (ratio > 0.4) return '#e6a23c'
          return '#409eff'
        },
      },
    }],
  }, true)
}

function initCharts() {
  if (trendChartRef.value) {
    trendChart.value = echarts.init(trendChartRef.value)
  }
  if (pieChartRef.value) {
    pieChart.value = echarts.init(pieChartRef.value)
  }
  if (peakChartRef.value) {
    peakChart.value = echarts.init(peakChartRef.value)
  }
}

function handleResize() {
  trendChart.value?.resize()
  pieChart.value?.resize()
  peakChart.value?.resize()
}

let refreshTimer = null

onMounted(async () => {
  try {
    const { data } = await channelApi.list()
    channels.value = data.channels || data || []
  } catch { /* empty */ }

  await nextTick()
  initCharts()
  fetchAll()

  refreshTimer = setInterval(fetchAll, 60000)
  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  if (refreshTimer) clearInterval(refreshTimer)
  window.removeEventListener('resize', handleResize)
  trendChart.value?.dispose()
  pieChart.value?.dispose()
  peakChart.value?.dispose()
})
</script>

<style scoped>
.analytics-page {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.filter-card {
  background: #fff;
}

.filter-card :deep(.el-card__body) {
  padding: 12px 20px;
}

.filter-bar {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 10px;
}

.summary-row {
  margin-bottom: 0;
}

.stat-card {
  border-left: 4px solid transparent;
  transition: transform 0.2s;
}

.stat-card:hover {
  transform: translateY(-2px);
}

.stat-total  { border-left-color: #409eff; }
.stat-crossline { border-left-color: #67c23a; }
.stat-counting { border-left-color: #e6a23c; }
.stat-channels { border-left-color: #f56c6c; }

.stat-content {
  display: flex;
  align-items: center;
  gap: 16px;
}

.stat-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 56px;
  height: 56px;
  border-radius: 12px;
  background: #f5f7fa;
}

.stat-total .stat-icon { color: #409eff; background: #ecf5ff; }
.stat-crossline .stat-icon { color: #67c23a; background: #f0f9eb; }
.stat-counting .stat-icon { color: #e6a23c; background: #fdf6ec; }
.stat-channels .stat-icon { color: #f56c6c; background: #fef0f0; }

.stat-value {
  font-size: 28px;
  font-weight: 700;
  color: #303133;
  line-height: 1;
}

.stat-label {
  font-size: 13px;
  color: #909399;
  margin-top: 6px;
}

.chart-row {
  margin-bottom: 0;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.chart-container {
  width: 100%;
  height: 320px;
}

.summary-row .el-col {
  margin-bottom: 12px;
}

.chart-row .el-col {
  margin-bottom: 16px;
}

@media (max-width: 768px) {
  .chart-container {
    height: 240px;
  }
  .stat-value {
    font-size: 22px;
  }
  .stat-icon {
    width: 40px;
    height: 40px;
    border-radius: 8px;
  }
  .stat-icon .el-icon {
    font-size: 24px !important;
  }
}
</style>
