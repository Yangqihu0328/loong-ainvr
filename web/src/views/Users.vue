<template>
  <div class="users-page">
    <el-card>
      <template #header>
        <div class="card-header">
          <span>用户管理</span>
          <el-button type="primary" @click="openCreateDialog">
            <el-icon><Plus /></el-icon>添加用户
          </el-button>
        </div>
      </template>

      <el-table :data="users" v-loading="loading" stripe>
        <el-table-column prop="id" label="ID" width="60" />
        <el-table-column prop="username" label="用户名" width="150" />
        <el-table-column prop="display_name" label="显示名称" width="150" />
        <el-table-column label="角色" width="120">
          <template #default="{ row }">
            <el-tag :type="getRoleTagType(row.role)" effect="plain">
              {{ getRoleLabel(row.role) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="状态" width="100">
          <template #default="{ row }">
            <el-tag :type="row.enabled ? 'success' : 'danger'" effect="dark" size="small">
              {{ row.enabled ? '启用' : '禁用' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="创建时间" width="180">
          <template #default="{ row }">
            {{ formatTime(row.created_at) }}
          </template>
        </el-table-column>
        <el-table-column label="最后登录" width="180">
          <template #default="{ row }">
            {{ row.last_login ? formatTime(row.last_login) : '从未登录' }}
          </template>
        </el-table-column>
        <el-table-column label="操作" fixed="right" width="200">
          <template #default="{ row }">
            <el-button size="small" @click="openEditDialog(row)">编辑</el-button>
            <el-button
              size="small"
              type="danger"
              :disabled="row.username === 'admin' && adminCount <= 1"
              @click="handleDelete(row)"
            >
              删除
            </el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <!-- Create / Edit User Dialog -->
    <el-dialog
      v-model="showDialog"
      :title="isEditing ? '编辑用户' : '添加用户'"
      width="480px"
    >
      <el-form
        ref="formRef"
        :model="form"
        :rules="formRules"
        label-width="80px"
      >
        <el-form-item label="用户名" prop="username">
          <el-input
            v-model="form.username"
            :disabled="isEditing"
            placeholder="请输入用户名"
          />
        </el-form-item>
        <el-form-item label="显示名称" prop="display_name">
          <el-input v-model="form.display_name" placeholder="请输入显示名称" />
        </el-form-item>
        <el-form-item :label="isEditing ? '新密码' : '密码'" prop="password">
          <el-input
            v-model="form.password"
            type="password"
            show-password
            :placeholder="isEditing ? '留空则不修改密码' : '请输入密码 (至少6位)'"
          />
        </el-form-item>
        <el-form-item label="角色" prop="role">
          <el-select v-model="form.role" style="width: 100%">
            <el-option label="管理员" value="admin" />
            <el-option label="操作员" value="operator" />
            <el-option label="观察者" value="viewer" />
          </el-select>
        </el-form-item>
        <el-form-item v-if="isEditing" label="状态">
          <el-switch v-model="form.enabled" active-text="启用" inactive-text="禁用" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showDialog = false">取消</el-button>
        <el-button type="primary" :loading="submitting" @click="handleSubmit">
          确定
        </el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { userApi } from '../api'

const users = ref([])
const loading = ref(false)
const showDialog = ref(false)
const submitting = ref(false)
const isEditing = ref(false)
const editingId = ref(null)
const formRef = ref(null)

const form = reactive({
  username: '',
  display_name: '',
  password: '',
  role: 'viewer',
  enabled: true,
})

const adminCount = computed(() => {
  return users.value.filter((u) => u.role === 'admin').length
})

const formRules = computed(() => ({
  username: isEditing.value
    ? []
    : [{ required: true, message: '请输入用户名', trigger: 'blur' }],
  password: isEditing.value
    ? [{ min: 6, message: '密码至少6位', trigger: 'blur' }]
    : [
        { required: true, message: '请输入密码', trigger: 'blur' },
        { min: 6, message: '密码至少6位', trigger: 'blur' },
      ],
  role: [{ required: true, message: '请选择角色', trigger: 'change' }],
}))

async function fetchUsers() {
  loading.value = true
  try {
    const { data } = await userApi.list()
    users.value = data
  } catch (err) {
    ElMessage.error('获取用户列表失败')
  } finally {
    loading.value = false
  }
}

function openCreateDialog() {
  isEditing.value = false
  editingId.value = null
  form.username = ''
  form.display_name = ''
  form.password = ''
  form.role = 'viewer'
  form.enabled = true
  showDialog.value = true
}

function openEditDialog(user) {
  isEditing.value = true
  editingId.value = user.id
  form.username = user.username
  form.display_name = user.display_name
  form.password = ''
  form.role = user.role
  form.enabled = user.enabled
  showDialog.value = true
}

async function handleSubmit() {
  if (!formRef.value) return
  try {
    await formRef.value.validate()
  } catch {
    return
  }

  submitting.value = true
  try {
    if (isEditing.value) {
      const updateData = {
        display_name: form.display_name,
        role: form.role,
        enabled: form.enabled,
      }
      if (form.password) {
        updateData.password = form.password
      }
      await userApi.update(editingId.value, updateData)
      ElMessage.success('用户更新成功')
    } else {
      await userApi.create({
        username: form.username,
        display_name: form.display_name || form.username,
        password: form.password,
        role: form.role,
      })
      ElMessage.success('用户创建成功')
    }
    showDialog.value = false
    await fetchUsers()
  } catch (err) {
    const msg = err.response?.data?.error || '操作失败'
    ElMessage.error(msg)
  } finally {
    submitting.value = false
  }
}

async function handleDelete(user) {
  try {
    await ElMessageBox.confirm(
      `确定要删除用户 "${user.display_name || user.username}" 吗？`,
      '警告',
      { confirmButtonText: '删除', cancelButtonText: '取消', type: 'warning' }
    )
  } catch {
    return
  }

  try {
    await userApi.delete(user.id)
    ElMessage.success('用户已删除')
    await fetchUsers()
  } catch (err) {
    const msg = err.response?.data?.error || '删除失败'
    ElMessage.error(msg)
  }
}

function getRoleLabel(role) {
  const map = { admin: '管理员', operator: '操作员', viewer: '观察者' }
  return map[role] || role
}

function getRoleTagType(role) {
  const map = { admin: 'danger', operator: 'warning', viewer: 'info' }
  return map[role] || 'info'
}

function formatTime(timestamp) {
  if (!timestamp) return ''
  return new Date(timestamp * 1000).toLocaleString('zh-CN')
}

onMounted(() => fetchUsers())
</script>

<style scoped>
.users-page {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
</style>
