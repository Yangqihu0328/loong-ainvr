<template>
  <div class="live-view">
    <el-card>
      <template #header>
        <div class="card-header">
          <span>实时预览</span>
          <div class="header-controls">
            <el-select
              v-model="preferProtocol"
              size="small"
              style="width: 100px; margin-right: 8px;"
            >
              <el-option label="WebRTC" value="webrtc" />
              <el-option label="FLV" value="flv" />
              <el-option label="HLS" value="hls" />
            </el-select>
            <el-select
              v-model="selectedLayout"
              size="small"
              :style="{ width: isMobile ? '90px' : '120px' }"
            >
              <el-option label="1 画面" :value="1" />
              <el-option label="2 画面" :value="2" />
              <el-option label="4 画面" :value="4" />
              <el-option v-if="!isMobile" label="9 画面" :value="9" />
              <el-option v-if="!isMobile" label="16 画面" :value="16" />
            </el-select>
            <!-- Mobile page indicator -->
            <span v-if="isMobile && totalPages > 1" class="page-indicator">
              {{ currentPage + 1 }}/{{ totalPages }}
            </span>
          </div>
        </div>
      </template>

      <div
        class="grid-container"
        :style="gridStyle"
        ref="gridRef"
        @touchstart="onTouchStart"
        @touchend="onTouchEnd"
      >
        <div
          v-for="(ch, idx) in pagedChannels"
          :key="ch ? ch.id : 'empty-' + idx"
          class="grid-cell"
        >
          <div v-if="ch" class="player-cell">
            <NvrPlayer :channel-id="ch.id" :channel-name="ch.name" :prefer-protocol="preferProtocol" />
          </div>
          <div v-else class="empty-cell">
            <el-icon :size="isMobile ? 24 : 32" color="#909399"><VideoCamera /></el-icon>
            <span>无信号</span>
          </div>
        </div>
      </div>

      <!-- Mobile page dots -->
      <div v-if="isMobile && totalPages > 1" class="page-dots">
        <span
          v-for="p in totalPages"
          :key="p"
          class="dot"
          :class="{ active: p - 1 === currentPage }"
          @click="currentPage = p - 1"
        />
      </div>
    </el-card>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useChannelStore } from '../stores/channel'
import NvrPlayer from '../components/NvrPlayer.vue'

const channelStore = useChannelStore()
const channels = computed(() =>
  channelStore.channels.filter((c) => c.state === 'Running')
)

const MOBILE_BREAKPOINT = 768
const isMobile = ref(window.innerWidth < MOBILE_BREAKPOINT)
function onResize() {
  isMobile.value = window.innerWidth < MOBILE_BREAKPOINT
  if (isMobile.value && selectedLayout.value > 4) {
    selectedLayout.value = 4
  }
}

const selectedLayout = ref(4)
const preferProtocol = ref('webrtc')
const currentPage = ref(0)

const totalPages = computed(() =>
  Math.max(1, Math.ceil(channels.value.length / selectedLayout.value))
)

const pagedChannels = computed(() => {
  const start = currentPage.value * selectedLayout.value
  const slice = channels.value.slice(start, start + selectedLayout.value)
  const result = []
  for (let i = 0; i < selectedLayout.value; i++) {
    result.push(slice[i] || null)
  }
  return result
})

const gridStyle = computed(() => {
  let cols
  if (selectedLayout.value <= 1) cols = 1
  else if (selectedLayout.value <= 2) cols = isMobile.value ? 1 : 2
  else cols = Math.ceil(Math.sqrt(selectedLayout.value))
  const rows = Math.ceil(selectedLayout.value / cols)
  return {
    gridTemplateColumns: `repeat(${cols}, 1fr)`,
    gridTemplateRows: `repeat(${rows}, 1fr)`,
  }
})

const gridRef = ref(null)
let touchStartX = 0
let touchStartY = 0
const SWIPE_THRESHOLD = 50

function onTouchStart(e) {
  if (!isMobile.value || totalPages.value <= 1) return
  const t = e.touches[0]
  touchStartX = t.clientX
  touchStartY = t.clientY
}

function onTouchEnd(e) {
  if (!isMobile.value || totalPages.value <= 1) return
  const t = e.changedTouches[0]
  const dx = t.clientX - touchStartX
  const dy = t.clientY - touchStartY
  if (Math.abs(dx) < SWIPE_THRESHOLD || Math.abs(dy) > Math.abs(dx)) return
  if (dx < 0 && currentPage.value < totalPages.value - 1) {
    currentPage.value++
  } else if (dx > 0 && currentPage.value > 0) {
    currentPage.value--
  }
}

onMounted(() => {
  channelStore.fetchChannels()
  window.addEventListener('resize', onResize)
})

onUnmounted(() => {
  window.removeEventListener('resize', onResize)
})
</script>

<style scoped>
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.header-controls {
  display: flex;
  align-items: center;
  gap: 8px;
}

.page-indicator {
  font-size: 12px;
  color: #909399;
  white-space: nowrap;
}

.grid-container {
  display: grid;
  gap: 4px;
  height: calc(100vh - 220px);
  touch-action: pan-y;
}

.grid-cell {
  background: #000;
  border-radius: 4px;
  overflow: hidden;
}

.player-cell {
  width: 100%;
  height: 100%;
}

.empty-cell {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100%;
  gap: 8px;
  color: #909399;
  font-size: 14px;
}

.page-dots {
  display: flex;
  justify-content: center;
  gap: 6px;
  padding: 10px 0 4px;
}

.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #dcdfe6;
  cursor: pointer;
  transition: background 0.2s, transform 0.2s;
}

.dot.active {
  background: #409eff;
  transform: scale(1.3);
}

@media (max-width: 768px) {
  .grid-container {
    height: calc(100vh - 200px);
    gap: 2px;
  }
  .empty-cell {
    font-size: 12px;
  }
}
</style>
