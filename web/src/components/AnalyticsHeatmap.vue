<template>
  <div class="heatmap-wrapper" ref="wrapperRef">
    <canvas ref="canvasRef" class="heatmap-canvas"></canvas>
    <div v-if="!cells || cells.length === 0" class="heatmap-empty">
      <el-empty description="暂无热力图数据" :image-size="80" />
    </div>
  </div>
</template>

<script setup>
import { ref, watch, onMounted, nextTick } from 'vue'

const props = defineProps({
  cells: { type: Array, default: () => [] },
  gridCols: { type: Number, default: 20 },
  gridRows: { type: Number, default: 15 },
})

const canvasRef = ref(null)
const wrapperRef = ref(null)

function heatColor(ratio) {
  // Gradient: blue → cyan → green → yellow → red
  const stops = [
    [0.0, [0, 0, 255]],
    [0.25, [0, 255, 255]],
    [0.5, [0, 255, 0]],
    [0.75, [255, 255, 0]],
    [1.0, [255, 0, 0]],
  ]

  let lower = stops[0], upper = stops[stops.length - 1]
  for (let i = 0; i < stops.length - 1; i++) {
    if (ratio >= stops[i][0] && ratio <= stops[i + 1][0]) {
      lower = stops[i]
      upper = stops[i + 1]
      break
    }
  }

  const t = (upper[0] - lower[0]) > 0
    ? (ratio - lower[0]) / (upper[0] - lower[0])
    : 0

  const r = Math.round(lower[1][0] + t * (upper[1][0] - lower[1][0]))
  const g = Math.round(lower[1][1] + t * (upper[1][1] - lower[1][1]))
  const b = Math.round(lower[1][2] + t * (upper[1][2] - lower[1][2]))

  return `rgba(${r}, ${g}, ${b}, 0.6)`
}

function draw() {
  const canvas = canvasRef.value
  const wrapper = wrapperRef.value
  if (!canvas || !wrapper) return

  const rect = wrapper.getBoundingClientRect()
  const dpr = window.devicePixelRatio || 1
  canvas.width = rect.width * dpr
  canvas.height = rect.height * dpr
  canvas.style.width = `${rect.width}px`
  canvas.style.height = `${rect.height}px`

  const ctx = canvas.getContext('2d')
  ctx.scale(dpr, dpr)
  ctx.clearRect(0, 0, rect.width, rect.height)

  if (!props.cells || props.cells.length === 0) return

  const cellW = rect.width / props.gridCols
  const cellH = rect.height / props.gridRows

  const maxCount = Math.max(...props.cells.map(c => c.count), 1)

  // Draw grid background
  ctx.strokeStyle = 'rgba(200, 200, 200, 0.15)'
  ctx.lineWidth = 0.5
  for (let x = 0; x <= props.gridCols; x++) {
    ctx.beginPath()
    ctx.moveTo(x * cellW, 0)
    ctx.lineTo(x * cellW, rect.height)
    ctx.stroke()
  }
  for (let y = 0; y <= props.gridRows; y++) {
    ctx.beginPath()
    ctx.moveTo(0, y * cellH)
    ctx.lineTo(rect.width, y * cellH)
    ctx.stroke()
  }

  // Draw heat cells with gaussian-like radial gradient for smoothing
  for (const cell of props.cells) {
    const ratio = cell.count / maxCount
    const cx = (cell.x + 0.5) * cellW
    const cy = (cell.y + 0.5) * cellH
    const radius = Math.max(cellW, cellH) * 0.8

    const gradient = ctx.createRadialGradient(cx, cy, 0, cx, cy, radius)
    const baseColor = heatColor(ratio)
    gradient.addColorStop(0, baseColor)
    gradient.addColorStop(1, 'rgba(0, 0, 0, 0)')

    ctx.fillStyle = gradient
    ctx.fillRect(
      cx - radius, cy - radius,
      radius * 2, radius * 2
    )
  }

  // Draw count labels for high-value cells
  ctx.font = '10px sans-serif'
  ctx.textAlign = 'center'
  ctx.textBaseline = 'middle'
  ctx.fillStyle = '#fff'
  for (const cell of props.cells) {
    if (cell.count / maxCount > 0.3) {
      const cx = (cell.x + 0.5) * cellW
      const cy = (cell.y + 0.5) * cellH
      ctx.fillText(String(cell.count), cx, cy)
    }
  }
}

watch(() => props.cells, () => nextTick(draw), { deep: true })

onMounted(() => {
  nextTick(draw)
  const observer = new ResizeObserver(() => draw())
  if (wrapperRef.value) observer.observe(wrapperRef.value)
})
</script>

<style scoped>
.heatmap-wrapper {
  position: relative;
  width: 100%;
  height: 320px;
  background: #1a1a2e;
  border-radius: 6px;
  overflow: hidden;
}

.heatmap-canvas {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
}

.heatmap-empty {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
}

.heatmap-empty :deep(.el-empty__description p) {
  color: #999;
}
</style>
