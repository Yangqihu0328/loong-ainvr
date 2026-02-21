import { ref } from 'vue'

export const swUpdateAvailable = ref(false)
export const swRegistration = ref(null)

let refreshing = false

export function registerServiceWorker() {
  if (!('serviceWorker' in navigator)) return

  window.addEventListener('load', async () => {
    try {
      const reg = await navigator.serviceWorker.register('/sw.js', { scope: '/' })
      swRegistration.value = reg

      reg.addEventListener('updatefound', () => {
        const newWorker = reg.installing
        if (!newWorker) return

        newWorker.addEventListener('statechange', () => {
          if (newWorker.state === 'installed' && navigator.serviceWorker.controller) {
            swUpdateAvailable.value = true
          }
        })
      })
    } catch (err) {
      console.warn('SW registration failed:', err)
    }
  })

  navigator.serviceWorker.addEventListener('controllerchange', () => {
    if (!refreshing) {
      refreshing = true
      window.location.reload()
    }
  })
}

export function applySwUpdate() {
  const reg = swRegistration.value
  if (!reg?.waiting) return
  reg.waiting.postMessage({ type: 'SKIP_WAITING' })
}

// --- Install prompt ---

let deferredPrompt = null
export const canInstall = ref(false)

window.addEventListener('beforeinstallprompt', (e) => {
  e.preventDefault()
  deferredPrompt = e
  canInstall.value = true
})

window.addEventListener('appinstalled', () => {
  canInstall.value = false
  deferredPrompt = null
})

export async function promptInstall() {
  if (!deferredPrompt) return false
  deferredPrompt.prompt()
  const { outcome } = await deferredPrompt.userChoice
  deferredPrompt = null
  canInstall.value = false
  return outcome === 'accepted'
}

// --- Push notifications ---

const VAPID_PUBLIC_KEY = ''

export const pushSupported = ref('PushManager' in window)
export const pushSubscribed = ref(false)

export async function subscribePush() {
  const reg = swRegistration.value
  if (!reg || !VAPID_PUBLIC_KEY) return null

  try {
    const sub = await reg.pushManager.subscribe({
      userVisibleOnly: true,
      applicationServerKey: urlBase64ToUint8Array(VAPID_PUBLIC_KEY),
    })
    pushSubscribed.value = true
    return sub
  } catch (err) {
    console.warn('Push subscription failed:', err)
    return null
  }
}

export async function unsubscribePush() {
  const reg = swRegistration.value
  if (!reg) return

  const sub = await reg.pushManager.getSubscription()
  if (sub) {
    await sub.unsubscribe()
    pushSubscribed.value = false
  }
}

function urlBase64ToUint8Array(base64String) {
  const padding = '='.repeat((4 - (base64String.length % 4)) % 4)
  const base64 = (base64String + padding).replace(/-/g, '+').replace(/_/g, '/')
  const raw = atob(base64)
  const arr = new Uint8Array(raw.length)
  for (let i = 0; i < raw.length; i++) {
    arr[i] = raw.charCodeAt(i)
  }
  return arr
}
