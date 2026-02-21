<template>
  <div class="faces-container">
    <div class="page-header">
      <h2>人脸分析</h2>
    </div>

    <!-- Search Filters -->
    <el-card shadow="never" class="filter-card">
      <el-form :inline="true" :model="query" class="filter-form">
        <el-form-item label="通道">
          <el-select v-model="query.channel_id" placeholder="全部" clearable style="width: 140px">
            <el-option v-for="ch in channels" :key="ch.id" :label="ch.name || `CH${ch.id}`" :value="ch.id" />
          </el-select>
        </el-form-item>
        <el-form-item label="性别">
          <el-select v-model="query.gender" placeholder="全部" clearable style="width: 100px">
            <el-option label="男" value="male" />
            <el-option label="女" value="female" />
          </el-select>
        </el-form-item>
        <el-form-item label="年龄范围">
          <el-input-number v-model="query.age_min" :min="0" :max="120" placeholder="最小" style="width: 90px" />
          <span style="margin: 0 4px">-</span>
          <el-input-number v-model="query.age_max" :min="0" :max="120" placeholder="最大" style="width: 90px" />
        </el-form-item>
        <el-form-item label="时间范围">
          <el-date-picker v-model="timeRange" type="datetimerange" range-separator="至"
            start-placeholder="开始" end-placeholder="结束"
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

    <!-- Stats -->
    <el-row :gutter="16" class="stats-row">
      <el-col :xs="12" :sm="6">
        <el-statistic title="查询结果" :value="totalCount" />
      </el-col>
    </el-row>

    <!-- Results Grid -->
    <div class="face-grid">
      <el-card v-for="face in faces" :key="face.id" shadow="hover" class="face-card">
        <div class="face-snapshot">
          <el-image v-if="face.snapshot_path" :src="face.snapshot_path"
            :preview-src-list="[face.snapshot_path]" fit="cover" />
          <el-icon v-else class="no-face-icon"><User /></el-icon>
        </div>
        <div class="face-info">
          <div class="face-attr">
            <el-tag size="small" :type="face.gender === 'male' ? '' : 'danger'">
              {{ face.gender === 'male' ? '男' : '女' }}
            </el-tag>
            <el-tag size="small" type="info">{{ face.age }}岁</el-tag>
          </div>
          <div class="face-meta">
            <span>CH{{ face.channel_id }}</span>
            <span>{{ formatTime(face.timestamp) }}</span>
          </div>
          <div class="face-confidence">
            置信度: {{ (face.confidence * 100).toFixed(1) }}%
          </div>
        </div>
      </el-card>
    </div>

    <div v-if="faces.length === 0 && !loading" class="empty-state">
      <el-empty description="暂无人脸记录" />
    </div>

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
import { Search, User } from '@element-plus/icons-vue'
import { faceApi, channelApi } from '../api'

const faces = ref([])
const channels = ref([])
const loading = ref(false)
const totalCount = ref(0)
const currentPage = ref(1)
const timeRange = ref(null)

const query = reactive({
  channel_id: null,
  gender: null,
  age_min: null,
  age_max: null,
  limit: 24,
  offset: 0,
})

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
    if (query.channel_id != null) params.channel_id = query.channel_id
    if (query.gender) params.gender = query.gender
    if (query.age_min != null) params.age_min = query.age_min
    if (query.age_max != null) params.age_max = query.age_max
    if (timeRange.value?.length === 2) {
      params.start_time = Math.floor(timeRange.value[0] / 1000)
      params.end_time = Math.floor(timeRange.value[1] / 1000)
    }
    const { data } = await faceApi.query(params)
    faces.value = data.faces || []
    totalCount.value = data.count || faces.value.length
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '查询失败')
  } finally {
    loading.value = false
  }
}

function resetQuery() {
  query.channel_id = null
  query.gender = null
  query.age_min = null
  query.age_max = null
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
.faces-container {
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
.face-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  gap: 16px;
}
.face-card {
  overflow: hidden;
}
.face-card :deep(.el-card__body) {
  padding: 0;
}
.face-snapshot {
  width: 100%;
  height: 180px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #f5f7fa;
}
.face-snapshot .el-image {
  width: 100%;
  height: 100%;
}
.no-face-icon {
  font-size: 48px;
  color: #c0c4cc;
}
.face-info {
  padding: 12px;
}
.face-attr {
  display: flex;
  gap: 6px;
  margin-bottom: 8px;
}
.face-meta {
  display: flex;
  justify-content: space-between;
  font-size: 12px;
  color: #909399;
  margin-bottom: 4px;
}
.face-confidence {
  font-size: 12px;
  color: #606266;
}
.empty-state {
  padding: 60px 0;
}
.pagination-bar {
  display: flex;
  justify-content: center;
  margin-top: 16px;
}
</style>
