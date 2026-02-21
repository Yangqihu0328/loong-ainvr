/**
 * Generates PWA icons as minimal PNG files.
 * These are placeholder icons with the Loong NVR logo (camera + "L" text).
 * For production, replace with professionally designed icons.
 *
 * Run: node scripts/generate-icons.js
 */
import { writeFileSync, mkdirSync } from 'fs'
import { dirname, join } from 'path'
import { fileURLToPath } from 'url'

const __dirname = dirname(fileURLToPath(import.meta.url))
const outDir = join(__dirname, '..', 'public', 'icons')
mkdirSync(outDir, { recursive: true })

const sizes = [72, 96, 128, 144, 152, 192, 384, 512]

function createSvg(size) {
  const r = size * 0.4
  const fontSize = size * 0.35
  const camW = size * 0.22
  const camH = size * 0.16
  const camX = size / 2 - camW / 2
  const camY = size * 0.2

  return `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 ${size} ${size}">
  <rect width="${size}" height="${size}" rx="${size * 0.18}" fill="#1d1e1f"/>
  <circle cx="${size / 2}" cy="${size / 2}" r="${r}" fill="#409eff" opacity="0.15"/>
  <rect x="${camX}" y="${camY}" width="${camW}" height="${camH}" rx="${size * 0.02}" fill="#409eff"/>
  <polygon points="${camX + camW},${camY + camH * 0.15} ${camX + camW + size * 0.08},${camY} ${camX + camW + size * 0.08},${camY + camH} ${camX + camW},${camY + camH * 0.85}" fill="#409eff"/>
  <text x="${size / 2}" y="${size * 0.72}" text-anchor="middle" font-family="Arial,sans-serif" font-weight="bold" font-size="${fontSize}" fill="#409eff">L</text>
</svg>`
}

for (const size of sizes) {
  const svg = createSvg(size)
  const filename = `icon-${size}x${size}.svg`
  writeFileSync(join(outDir, filename), svg, 'utf8')
  console.log(`Generated ${filename}`)
}

console.log(`\nNote: SVG icons generated. For PNG conversion, use:`)
console.log(`  npx sharp-cli -i public/icons/icon-512x512.svg -o public/icons/icon-512x512.png resize 512 512`)
console.log(`Or replace with actual PNG assets for production.`)
