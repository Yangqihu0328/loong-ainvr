import { defineStore } from 'pinia'
import { ref } from 'vue'
import { systemApi } from '../api'

export const useSystemStore = defineStore('system', () => {
  const status = ref({
    version: '',
    channels_active: 0,
    max_channels: 64,
  })

  async function fetchStatus() {
    try {
      const { data } = await systemApi.status()
      status.value = data
    } catch (err) {
      console.error('Failed to fetch system status:', err)
    }
  }

  return { status, fetchStatus }
})
