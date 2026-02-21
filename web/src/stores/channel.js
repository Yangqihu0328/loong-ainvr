import { defineStore } from 'pinia'
import { ref } from 'vue'
import { channelApi } from '../api'

export const useChannelStore = defineStore('channel', () => {
  const channels = ref([])
  const loading = ref(false)
  const error = ref(null)

  async function fetchChannels() {
    loading.value = true
    error.value = null
    try {
      const { data } = await channelApi.list()
      channels.value = data
    } catch (err) {
      error.value = err.response?.data?.error || err.message
      console.error('Failed to fetch channels:', err)
    } finally {
      loading.value = false
    }
  }

  async function createChannel(config) {
    try {
      const { data } = await channelApi.create(config)
      if (data.success) {
        await fetchChannels()
      }
      return data
    } catch (err) {
      error.value = err.response?.data?.error || err.message
      throw err
    }
  }

  async function updateChannel(id, config) {
    try {
      const { data } = await channelApi.update(id, config)
      if (data.success) {
        await fetchChannels()
      }
      return data
    } catch (err) {
      error.value = err.response?.data?.error || err.message
      throw err
    }
  }

  async function deleteChannel(id) {
    try {
      await channelApi.delete(id)
      await fetchChannels()
    } catch (err) {
      error.value = err.response?.data?.error || err.message
      throw err
    }
  }

  async function startChannel(id) {
    try {
      await channelApi.start(id)
      await fetchChannels()
    } catch (err) {
      error.value = err.response?.data?.error || err.message
      throw err
    }
  }

  async function stopChannel(id) {
    try {
      await channelApi.stop(id)
      await fetchChannels()
    } catch (err) {
      error.value = err.response?.data?.error || err.message
      throw err
    }
  }

  return {
    channels,
    loading,
    error,
    fetchChannels,
    createChannel,
    updateChannel,
    deleteChannel,
    startChannel,
    stopChannel,
  }
})
