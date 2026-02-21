<template>
  <div class="plugins-page">
    <el-card>
      <template #header>
        <div class="card-header">
          <span>AI 模型插件管理</span>
          <div class="header-actions">
            <el-button type="primary" size="small" @click="refresh" :loading="loading">
              <el-icon><Refresh /></el-icon>
              刷新
            </el-button>
            <el-upload
              :action="uploadUrl"
              :headers="uploadHeaders"
              name="plugin"
              accept=".so"
              :show-file-list="false"
              :on-success="onUploadSuccess"
              :on-error="onUploadError"
              :before-upload="beforeUpload"
            >
              <el-button type="success" size="small">
                <el-icon><Upload /></el-icon>
                上传插件
              </el-button>
            </el-upload>
          </div>
        </div>
      </template>

      <el-alert
        v-if="plugins.length === 0 && !loading"
        title="暂无已安装插件"
        description="将 .so 共享库文件上传到插件目录或使用上方按钮上传。插件须实现 ModelPlugin 接口并导出 loong_create_plugin / loong_destroy_plugin 入口。"
        type="info"
        show-icon
        :closable="false"
        style="margin-bottom: 16px"
      />

      <el-table :data="plugins" stripe v-loading="loading" empty-text="暂无插件">
        <el-table-column prop="name" label="插件名称" min-width="120" />
        <el-table-column prop="version" label="版本" width="80" />
        <el-table-column prop="model_family" label="模型族" min-width="100">
          <template #default="{ row }">
            <el-tag size="small" type="info">{{ row.model_family }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="author" label="作者" min-width="100" show-overflow-tooltip />
        <el-table-column prop="description" label="描述" min-width="180" show-overflow-tooltip />
        <el-table-column prop="api_version" label="API" width="60" align="center" />
      </el-table>
    </el-card>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import api from '../api'

const loading = ref(false)
const plugins = ref([])

const uploadUrl = '/api/plugins/upload'
const uploadHeaders = computed(() => ({
  Authorization: `Bearer ${localStorage.getItem('token') || ''}`,
}))

async function refresh() {
  loading.value = true
  try {
    const { data } = await api.get('/plugins')
    plugins.value = data.plugins || []
  } catch {
    ElMessage.error('获取插件列表失败')
  } finally {
    loading.value = false
  }
}

function beforeUpload(file) {
  if (!file.name.endsWith('.so')) {
    ElMessage.error('仅支持 .so 共享库文件')
    return false
  }
  if (file.size > 100 * 1024 * 1024) {
    ElMessage.error('文件大小不能超过 100MB')
    return false
  }
  return true
}

function onUploadSuccess(response) {
  if (response.success) {
    ElMessage.success(`插件 ${response.file} 上传并加载成功`)
    refresh()
  } else {
    ElMessage.error('插件加载失败（文件已保存）')
  }
}

function onUploadError() {
  ElMessage.error('插件上传失败')
}

onMounted(() => refresh())
</script>

<style scoped>
.plugins-page {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 8px;
}

.header-actions {
  display: flex;
  gap: 8px;
}
</style>
