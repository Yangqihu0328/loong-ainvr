<template>
  <div class="events-page">
    <el-card>
      <template #header>
        <span>报警事件</span>
      </template>

      <el-form :inline="true" class="filter-form">
        <el-form-item label="通道">
          <el-select v-model="query.channel_id" placeholder="全部通道" clearable>
            <el-option
              v-for="ch in channels"
              :key="ch.id"
              :label="`CH${ch.id} - ${ch.name}`"
              :value="ch.id"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="事件类型">
          <el-select v-model="query.event_type" placeholder="全部类型" clearable>
            <el-option label="人员检测" value="person_detected" />
            <el-option label="车辆检测" value="vehicle_detected" />
            <el-option label="异常行为" value="abnormal_behavior" />
          </el-select>
        </el-form-item>
        <el-form-item label="时间">
          <el-date-picker
            v-model="query.dateRange"
            type="datetimerange"
            range-separator="至"
            start-placeholder="开始"
            end-placeholder="结束"
          />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" @click="handleSearch">查询</el-button>
        </el-form-item>
      </el-form>

      <el-table :data="events" stripe v-loading="searchLoading" empty-text="暂无报警事件，请选择查询条件后点击查询">
        <el-table-column prop="channel_id" label="通道" width="80" />
        <el-table-column label="时间" width="180">
          <template #default="{ row }">
            {{ formatTime(row.event_time) }}
          </template>
        </el-table-column>
        <el-table-column prop="event_type" label="事件类型" width="140">
          <template #default="{ row }">
            <el-tag :type="eventTagType(row.event_type)" size="small">
              {{ eventLabel(row.event_type) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="置信度" width="100">
          <template #default="{ row }">
            {{ (row.confidence * 100).toFixed(1) }}%
          </template>
        </el-table-column>
        <el-table-column label="操作" width="120">
          <template #default="{ row }">
            <el-button type="primary" size="small" text @click="handleViewRecording(row)">
              查看录像
            </el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup>
import { ref, computed, reactive, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { useChannelStore } from '../stores/channel'
import { recordingApi } from '../api'

const router = useRouter()

const channelStore = useChannelStore()
const channels = computed(() => channelStore.channels)
const events = ref([])
const searchLoading = ref(false)

const query = reactive({
  channel_id: null,
  event_type: null,
  dateRange: null,
})

async function handleSearch() {
  searchLoading.value = true
  try {
    const params = {}
    if (query.channel_id) params.channel_id = query.channel_id
    if (query.event_type) params.event_type = query.event_type
    if (query.dateRange) {
      params.start_time = query.dateRange[0].getTime()
      params.end_time = query.dateRange[1].getTime()
    }
    const { data } = await recordingApi.events(params)
    events.value = data || []
  } catch {
    events.value = []
    ElMessage.error('查询失败')
  } finally {
    searchLoading.value = false
  }
}

function formatTime(ts) {
  return ts ? new Date(ts).toLocaleString('zh-CN') : '-'
}

function eventTagType(type) {
  return { person_detected: 'warning', vehicle_detected: 'success' }[type] || 'info'
}

function eventLabel(type) {
  const map = {
    person_detected: '人员检测',
    vehicle_detected: '车辆检测',
    abnormal_behavior: '异常行为',
  }
  return map[type] || type
}

function handleViewRecording(row) {
  router.push({
    path: '/recordings',
    query: {
      channel_id: row.channel_id,
      time: row.event_time,
    },
  })
}

onMounted(() => channelStore.fetchChannels())
</script>

<style scoped>
.filter-form {
  margin-bottom: 16px;
}
</style>
