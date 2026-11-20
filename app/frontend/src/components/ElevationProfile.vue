<script setup lang="ts">
import { computed, ref, useId } from 'vue'
import type { Point } from '../api'
import { steepnessSections, type ProfilePoint } from '../elevation'
import { num } from '../config'

const props = defineProps<{ profile: ProfilePoint[] }>()
const emit = defineEmits<{ hover: [pos: Point | null] }>()

const W = 600
const H = 120
const PAD_TOP = 8
const PAD_BOTTOM = 8

const total = computed(() => props.profile.at(-1)?.d ?? 0)
const range = computed(() => {
  const eles = props.profile.map((p) => p.ele)
  const min = eles.reduce((m, v) => Math.min(m, v), Infinity)
  const max = eles.reduce((m, v) => Math.max(m, v), -Infinity)
  return { min, max, span: Math.max(max - min, 20) } // keep flat routes from looking mountainous
})

const x = (d: number) => (total.value > 0 ? (d / total.value) * W : 0)
const y = (ele: number) => PAD_TOP + (1 - (ele - range.value.min) / range.value.span) * (H - PAD_TOP - PAD_BOTTOM)

const line = computed(() => props.profile.map((p) => `${x(p.d).toFixed(1)},${y(p.ele).toFixed(1)}`).join(' L'))

// Use hard stops in a horizontal gradient (two stops per grade segment).
const sections = computed(() => steepnessSections(props.profile))
const gradientId = useId()

// Binary-search the first profile point at or after hovered distance d.
const cursor = ref<number | null>(null)
function onMove(ev: PointerEvent) {
  const rect = (ev.currentTarget as SVGElement).getBoundingClientRect()
  const d = ((ev.clientX - rect.left) / rect.width) * total.value
  const p = props.profile
  let lo = 0
  let hi = p.length - 1
  while (lo < hi) {
    const mid = (lo + hi) >> 1
    if (p[mid]!.d < d) {
      lo = mid + 1
    } else {
      hi = mid
    }
  }
  cursor.value = lo
  emit('hover', [p[lo]!.pos[0], p[lo]!.pos[1]])
}
function onLeave() {
  cursor.value = null
  emit('hover', null)
}

const at = computed(() => (cursor.value === null ? null : props.profile[cursor.value]!))
</script>

<template>
  <div class="profile">
    <svg
      :viewBox="`0 0 ${W} ${H}`"
      preserveAspectRatio="none"
      role="img"
      :aria-label="`Elevation profile from ${num(range.min)} to ${num(range.max)} m over ${num(total / 1000, 1)} km`"
      @pointermove="onMove"
      @pointerleave="onLeave"
    >
      <defs>
        <linearGradient :id="gradientId" gradientUnits="userSpaceOnUse" x1="0" :x2="W" y1="0" y2="0">
          <template v-for="s in sections" :key="s.from">
            <stop :offset="s.from / total" :class="s.level" />
            <stop :offset="s.to / total" :class="s.level" />
          </template>
        </linearGradient>
      </defs>
      <path :d="`M0,${H - PAD_BOTTOM} L${line} L${W},${H - PAD_BOTTOM} Z`" class="area" :fill="`url(#${gradientId})`" />
      <path :d="`M${line}`" class="line" :stroke="`url(#${gradientId})`" />
      <line v-if="at" :x1="x(at.d)" :x2="x(at.d)" :y1="0" :y2="H" class="cursor" />
    </svg>
    <div class="axis">
      <span>{{ num(range.min) }}–{{ num(range.max) }} m</span>
      <span v-if="at">{{ num(at.d / 1000, 1) }} km · {{ num(at.ele) }} m</span>
      <span>{{ num(total / 1000, 1) }} km</span>
    </div>
  </div>
</template>

<style scoped>
.profile {
  display: grid;
  gap: 2px;
}

svg {
  display: block;
  width: 100%;
  height: 80px;
  cursor: crosshair;
}

.flat {
  stop-color: #3fa34d;
}

.moderate {
  stop-color: #f2c037;
}

.steep {
  stop-color: #d93a34;
}

.area {
  fill-opacity: 0.6;
}

.line {
  fill: none;
  stroke-width: 2;
  vector-effect: non-scaling-stroke;
}

.cursor {
  stroke: var(--ink);
  stroke-width: 1;
  vector-effect: non-scaling-stroke;
}

.axis {
  display: flex;
  justify-content: space-between;
  font-size: 12px;
  color: var(--muted);
  font-variant-numeric: tabular-nums;
}
</style>
