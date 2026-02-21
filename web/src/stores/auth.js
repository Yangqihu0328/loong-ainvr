import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { authApi } from '../api'

export const useAuthStore = defineStore('auth', () => {
  const token = ref(localStorage.getItem('token') || '')
  const user = ref(JSON.parse(localStorage.getItem('user') || 'null'))

  const isAuthenticated = computed(() => !!token.value)
  const isAdmin = computed(() => user.value?.role === 'admin')
  const isOperator = computed(
    () => user.value?.role === 'admin' || user.value?.role === 'operator'
  )

  async function login(username, password) {
    const { data } = await authApi.login(username, password)
    token.value = data.token
    user.value = data.user
    localStorage.setItem('token', data.token)
    localStorage.setItem('user', JSON.stringify(data.user))
    return data
  }

  function logout() {
    token.value = ''
    user.value = null
    localStorage.removeItem('token')
    localStorage.removeItem('user')
  }

  async function fetchProfile() {
    try {
      const { data } = await authApi.profile()
      user.value = data
      localStorage.setItem('user', JSON.stringify(data))
    } catch (err) {
      if (err.response?.status === 401) {
        logout()
      }
    }
  }

  async function changePassword(oldPassword, newPassword) {
    await authApi.changePassword(oldPassword, newPassword)
  }

  return {
    token,
    user,
    isAuthenticated,
    isAdmin,
    isOperator,
    login,
    logout,
    fetchProfile,
    changePassword,
  }
})
