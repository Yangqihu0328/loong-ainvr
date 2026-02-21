<template>
  <div class="nvr-player" @dblclick="toggleFullscreen">
    <video ref="videoEl" autoplay muted playsinline class="video-element" />
    <div class="player-overlay">
      <span class="channel-label">{{ channelName }} (CH{{ channelId }})</span>
      <span v-if="status === 'loading'" class="status-label">
        <el-icon class="is-loading"><Loading /></el-icon>
        连接中...
      </span>
      <span v-if="status === 'error'" class="status-label error">
        连接失败
      </span>
      <span v-if="protocol" class="protocol-badge" :class="protocolClass">{{ protocol }}</span>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onBeforeUnmount, watch } from 'vue'

const props = defineProps({
  channelId: { type: Number, required: true },
  channelName: { type: String, default: '' },
  preferProtocol: { type: String, default: 'webrtc' },
})

const videoEl = ref(null)
const status = ref('loading')
const protocol = ref('')
let player = null
let hlsInstance = null
let rtcPeerConnection = null
let rtcSessionId = null

const protocolClass = computed(() => ({
  webrtc: protocol.value === 'WebRTC',
  flv: protocol.value === 'FLV',
  hls: protocol.value === 'HLS',
}))

async function initPlayer() {
  status.value = 'loading'
  protocol.value = ''

  const token = localStorage.getItem('token') || ''
  const strategies = getProtocolOrder()

  for (const tryFn of strategies) {
    const ok = await tryFn(token)
    if (ok) return
  }

  status.value = 'error'
}

function getProtocolOrder() {
  switch (props.preferProtocol) {
    case 'webrtc':
      return [tryWebRtc, tryFlv, tryHls]
    case 'flv':
      return [tryFlv, tryWebRtc, tryHls]
    case 'hls':
      return [tryHls, tryFlv, tryWebRtc]
    default:
      return [tryWebRtc, tryFlv, tryHls]
  }
}

// ---- WebRTC ----

async function tryWebRtc(token) {
  try {
    if (!window.RTCPeerConnection) return false

    const pc = new RTCPeerConnection({
      iceServers: [{ urls: 'stun:stun.l.google.com:19302' }],
    })

    pc.addTransceiver('video', { direction: 'recvonly' })

    pc.ontrack = (event) => {
      if (event.streams && event.streams[0] && videoEl.value) {
        videoEl.value.srcObject = event.streams[0]
        status.value = 'playing'
        protocol.value = 'WebRTC'
      }
    }

    pc.onconnectionstatechange = () => {
      if (pc.connectionState === 'failed' || pc.connectionState === 'disconnected') {
        if (protocol.value === 'WebRTC') {
          destroyWebRtc()
          initPlayer()
        }
      }
    }

    const offer = await pc.createOffer()
    await pc.setLocalDescription(offer)

    // Wait for ICE gathering (trickle or complete)
    await new Promise((resolve) => {
      if (pc.iceGatheringState === 'complete') {
        resolve()
      } else {
        const check = () => {
          if (pc.iceGatheringState === 'complete') {
            pc.removeEventListener('icegatheringstatechange', check)
            resolve()
          }
        }
        pc.addEventListener('icegatheringstatechange', check)
        setTimeout(resolve, 3000)
      }
    })

    const localDesc = pc.localDescription
    const headers = { 'Content-Type': 'application/json' }
    if (token) headers['Authorization'] = `Bearer ${token}`

    const resp = await fetch('/api/webrtc/offer', {
      method: 'POST',
      headers,
      body: JSON.stringify({
        channel_id: props.channelId,
        sdp: localDesc.sdp,
      }),
    })

    if (!resp.ok) return false

    const data = await resp.json()
    if (!data.sdp) return false

    await pc.setRemoteDescription({ type: 'answer', sdp: data.sdp })

    rtcPeerConnection = pc
    rtcSessionId = data.session_id
    return true
  } catch {
    destroyWebRtc()
    return false
  }
}

function destroyWebRtc() {
  if (rtcPeerConnection) {
    rtcPeerConnection.close()
    rtcPeerConnection = null
  }
  if (rtcSessionId) {
    const token = localStorage.getItem('token') || ''
    fetch(`/api/webrtc/${rtcSessionId}`, {
      method: 'DELETE',
      headers: token ? { Authorization: `Bearer ${token}` } : {},
    }).catch(() => {})
    rtcSessionId = null
  }
  if (videoEl.value) {
    videoEl.value.srcObject = null
  }
}

// ---- FLV ----

async function tryFlv(token) {
  try {
    const flvjs = await import('flv.js')
    if (!flvjs.default.isSupported()) return false

    player = flvjs.default.createPlayer({
      type: 'flv',
      isLive: true,
      url: `/live/ch${props.channelId}.flv?token=${encodeURIComponent(token)}`,
    })

    player.attachMediaElement(videoEl.value)
    player.load()
    player.play()

    player.on('error', () => {
      status.value = 'error'
    })

    player.on('statistics_info', () => {
      if (status.value !== 'playing') {
        status.value = 'playing'
        protocol.value = 'FLV'
      }
    })

    return true
  } catch {
    return false
  }
}

// ---- HLS ----

async function tryHls(token) {
  try {
    const Hls = (await import('hls.js')).default
    if (!Hls.isSupported() && !videoEl.value?.canPlayType('application/vnd.apple.mpegurl')) {
      return false
    }

    const url = `/live/ch${props.channelId}/index.m3u8?token=${encodeURIComponent(token)}`

    if (Hls.isSupported()) {
      hlsInstance = new Hls({
        liveSyncDurationCount: 3,
        liveMaxLatencyDurationCount: 6,
        enableWorker: true,
        lowLatencyMode: true,
      })
      hlsInstance.loadSource(url)
      hlsInstance.attachMedia(videoEl.value)
      hlsInstance.on(Hls.Events.MANIFEST_PARSED, () => {
        videoEl.value.play()
        status.value = 'playing'
        protocol.value = 'HLS'
      })
      hlsInstance.on(Hls.Events.ERROR, (_event, data) => {
        if (data.fatal) {
          status.value = 'error'
        }
      })
    } else {
      videoEl.value.src = url
      videoEl.value.addEventListener('loadedmetadata', () => {
        videoEl.value.play()
        status.value = 'playing'
        protocol.value = 'HLS'
      })
    }

    return true
  } catch {
    return false
  }
}

// ---- Lifecycle ----

function destroyPlayer() {
  destroyWebRtc()
  if (player) {
    player.pause()
    player.unload()
    player.detachMediaElement()
    player.destroy()
    player = null
  }
  if (hlsInstance) {
    hlsInstance.destroy()
    hlsInstance = null
  }
}

function toggleFullscreen() {
  if (videoEl.value) {
    if (document.fullscreenElement) {
      document.exitFullscreen()
    } else {
      videoEl.value.requestFullscreen()
    }
  }
}

watch(() => props.channelId, () => {
  destroyPlayer()
  initPlayer()
})

watch(() => props.preferProtocol, () => {
  destroyPlayer()
  initPlayer()
})

onMounted(() => initPlayer())
onBeforeUnmount(() => destroyPlayer())
</script>

<style scoped>
.nvr-player {
  position: relative;
  width: 100%;
  height: 100%;
  background: #000;
}

.video-element {
  width: 100%;
  height: 100%;
  object-fit: contain;
}

.player-overlay {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  padding: 8px 12px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  background: linear-gradient(180deg, rgba(0,0,0,0.6) 0%, transparent 100%);
  pointer-events: none;
}

.channel-label {
  color: #fff;
  font-size: 12px;
  text-shadow: 1px 1px 2px rgba(0,0,0,0.8);
}

.status-label {
  color: #e6a23c;
  font-size: 12px;
  display: flex;
  align-items: center;
  gap: 4px;
}

.status-label.error {
  color: #f56c6c;
}

.protocol-badge {
  color: #67c23a;
  font-size: 10px;
  font-weight: 600;
  background: rgba(0,0,0,0.5);
  padding: 2px 6px;
  border-radius: 3px;
  letter-spacing: 0.5px;
}

.protocol-badge.webrtc {
  color: #409eff;
}

.protocol-badge.flv {
  color: #67c23a;
}

.protocol-badge.hls {
  color: #e6a23c;
}
</style>
