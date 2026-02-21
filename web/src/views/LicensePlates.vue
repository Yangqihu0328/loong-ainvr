<template>
  <div class="plates-container">
    <div class="page-header">
      <h2>车牌识别记录</h2>
    </div>

    <!-- Search Filters -->
    <el-card shadow="never" class="filter-card">
      <el-form :inline="true" :model="query" class="filter-form">
        <el-form-item label="车牌号">
          <el-input v-model="query.plate_number" placeholder="模糊搜索，如: 京A" clearable style="width: 180px"
            @keyup.enter="search" />
        </el-form-item>
        <el-form-item label="通道">
          <el-select v-model="query.channel_id" placeholder="全部" clearable style="width: 140px">
            <el-option v-for="ch in channels" :key="ch.id" :label="ch.name || `CH${ch.id}`" :value="ch.id" />
          </el-select>
        </el-form-item>
        <el-form-item label="时间范围">
          <el-date-picker v-model="timeRange" type="datetimerange" range-separator="至"
            start-placeholder="开始时间" end-placeholder="结束时间"
            value-format="x" style="width: 340px" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" @click="search">
            <el-icon><Search /></el-icon> 查询
          </el-button>
          <el-button @click="resetQuery">重置</el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <!-- Stats Summary -->
    <el-row :gutter="16" class="stats-row">
      <el-col :xs="12" :sm="6">
        <el-statistic title="查询结果" :value="totalCount" />
      </el-col>
    </el-row>

    <!-- Results Table -->
    <el-table :data="plates" v-loading="loading" border stripe style="width: 100%">
      <el-table-column prop="plate_number" label="车牌号码" width="160">
        <template #default="{ row }">
          <span class="plate-number" :class="plateColorClass(row.plate_color)">
            {{ row.plate_number }}
          </span>
        </template>
      </el-table-column>
      <el-table-column prop="plate_color" label="车牌颜色" width="100">
        <template #default="{ row }">
          <el-tag :type="colorTagType(row.plate_color)" size="small">
            {{ colorLabel(row.plate_color) }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column prop="confidence" label="置信度" width="100">
        <template #default="{ row }">
          {{ (row.confidence * 100).toFixed(1) }}%
        </template>
      </el-table-column>
      <el-table-column prop="channel_id" label="通道" width="80">
        <template #default="{ row }">
          CH{{ row.channel_id }}
        </template>
      </el-table-column>
      <el-table-column label="时间" min-width="180">
        <template #default="{ row }">
          {{ formatTime(row.timestamp) }}
        </template>
      </el-table-column>
      <el-table-column label="抓拍" width="100">
        <template #default="{ row }">
          <el-image v-if="row.snapshot_path" :src="row.snapshot_path"
            :preview-src-list="[row.snapshot_path]"
            fit="cover" style="width: 60px; height: 40px; border-radius: 4px;" />
          <span v-else class="no-snapshot">--</span>
        </template>
      </el-table-column>
    </el-table>

    <!-- Pagination -->
    <div class="pagination-bar" v-if="totalCount > query.limit">
      <el-pagination
        v-model:current-page="currentPage"
        :page-size="query.limit"
        :total="totalCount"
        layout="prev, pager, next, total"
        @current-change="onPageChange"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { Search } from '@element-plus/icons-vue'
import { plateApi, channelApi } from '../api'

const plates = ref([])
const channels = ref([])
const loading = ref(false)
const totalCount = ref(0)
const currentPage = ref(1)
const timeRange = ref(null)

const query = reactive({
  plate_number: '',
  channel_id: null,
  start_time: null,
  end_time: null,
  limit: 20,
  offset: 0,
})

const colorMap = {
  blue_plate: { label: '蓝牌', tag: '', css: 'blue' },
  green_plate: { label: '绿牌', tag: 'success', css: 'green' },
  yellow_plate: { label: '黄牌', tag: 'warning', css: 'yellow' },
  white_plate: { label: '白牌', tag: 'info', css: 'white' },
}

function plateColorClass(color) {
  return colorMap[color]?.css || ''
}

function colorTagType(color) {
  return colorMap[color]?.tag || 'info'
}

function colorLabel(color) {
  return colorMap[color]?.label || color || '未知'
}

function formatTime(ts) {
  if (!ts) return '--'
  return new Date(ts * 1000).toLocaleString('zh-CN')
}

async function loadChannels() {
  try {
    const { data } = await channelApi.list()
    channels.value = data.channels || data || []
  } catch { /* ignore */ }
}

async function search() {
  loading.value = true
  try {
    const params = { limit: query.limit, offset: query.offset }
    if (query.plate_number) params.plate_number = query.plate_number
    if (query.channel_id != null) params.channel_id = query.channel_id
    if (timeRange.value?.length === 2) {
      params.start_time = Math.floor(timeRange.value[0] / 1000)
      params.end_time = Math.floor(timeRange.value[1] / 1000)
    }
    const { data } = await plateApi.query(params)
    plates.value = data.plates || []
    totalCount.value = data.count || plates.value.length
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '查询失败')
  } finally {
    loading.value = false
  }
}

function resetQuery() {
  query.plate_number = ''
  query.channel_id = null
  query.offset = 0
  timeRange.value = null
  currentPage.value = 1
  search()
}

function onPageChange(page) {
  query.offset = (page - 1) * query.limit
  search()
}

onMounted(() => {
  loadChannels()
  search()
})
</script>

<style scoped>
.plates-container {
  padding: 20px;
}
.page-header {
  margin-bottom: 16px;
}
.page-header h2 {
  margin: 0;
  font-size: 20px;
}
.filter-card {
  margin-bottom: 16px;
}
.filter-form {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.stats-row {
  margin-bottom: 16px;
}
.plate-number {
  font-family: 'Courier New', monospace;
  font-weight: 700;
  font-size: 15px;
  padding: 2px 8px;
  border-radius: 4px;
}
.plate-number.blue {
  background: #1a5fb4;
  color: #fff;
}
.plate-number.green {
  background: #26a269;
  color: #fff;
}
.plate-number.yellow {
  background: #e5a50a;
  color: #000;
}
.plate-number.white {
  background: #f0f0f0;
  color: #333;
  border: 1px solid #ccc;
}
.no-snapshot {
  color: #999;
}
.pagination-bar {
  display: flex;
  justify-content: center;
  margin-top: 16px;
}
</style>
