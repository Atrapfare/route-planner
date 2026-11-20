<script setup lang="ts">
import { computed, ref } from 'vue'
import type { Point, TripResult } from '../api'
import { buildProfile, downloadGpx, ridingTime, steepestGrade } from '../elevation'
import { ROUTE_COLORS, num } from '../config'
import ElevationProfile from './ElevationProfile.vue'

const props = defineProps<{ result: TripResult; active: number }>()
const emit = defineEmits<{ select: [index: number]; hover: [pos: Point | null] }>()

const open = ref(false) // details expanded; stays as it is when switching routes

const trips = computed(() => props.result.routes)
const trip = computed(() => trips.value[props.active]!)
const color = (i: number) => ROUTE_COLORS[i % ROUTE_COLORS.length]!
const name = computed(() => `Round trip ${props.active + 1}`)
const profile = computed(() => buildProfile(trip.value.path))
const steepest = computed(() => steepestGrade(profile.value))

const SHARES = [
  { key: 'bike_pct', label: 'Bike route' },
  { key: 'nature_pct', label: 'Nature' },
  { key: 'unpaved_pct', label: 'Unpaved' },
] as const
</script>

<template>
  <section class="panel" :style="{ '--route': color(active) }" aria-label="Round trips">
    <div class="top">
      <div class="tabs" role="tablist">
        <button
          v-for="(t, i) in trips"
          :key="i"
          type="button"
          role="tab"
          class="tab"
          :aria-selected="active === i"
          :style="{ '--tab': color(i) }"
          @click="emit('select', i)"
        >
          <span class="swatch"></span>{{ i + 1 }} · {{ num(t.length_m / 1000, 1) }} km
          <span v-if="!t.within_tolerance" class="miss" title="Target length missed">!</span>
        </button>
      </div>
      <button type="button" class="toggle" :aria-expanded="open" @click="open = !open">
        {{ open ? 'Less' : 'Details' }} <span aria-hidden="true">{{ open ? '▾' : '▴' }}</span>
      </button>
    </div>

    <p class="line">
      <strong>{{ num(trip.length_m / 1000, 1) }} km</strong>
      <span>{{ num(trip.ascent_m) }} m climb</span>
      <span>~{{ ridingTime(trip.length_m, trip.ascent_m) }}</span>
      <span>Bike route {{ num(trip.bike_pct) }} %</span>
      <span>Nature {{ num(trip.nature_pct) }} %</span>
      <span v-if="!trip.within_tolerance" class="warning">target length missed</span>
    </p>
    <p v-if="result.poi_tour" class="note">
      The tour through the POIs alone is already longer than requested:
      {{ num(trips[0]!.length_m / 1000, 1) }} km.
    </p>

    <div v-if="open" class="details">
      <div class="metrics">
        <template v-for="share in SHARES" :key="share.key">
          <span>{{ share.label }}</span>
          <span class="bar"><span :style="{ width: `${Math.min(trip[share.key], 100)}%` }"></span></span>
          <span class="value">{{ num(trip[share.key]) }} %</span>
        </template>
        <span>Curviness</span>
        <span></span>
        <span class="value">{{ num(trip.curvy_deg_km) }} °/km</span>
        <span>Ridden twice</span>
        <span></span>
        <span class="value">{{ num(trip.overlap_pct, 1) }} %</span>
        <template v-if="steepest !== null">
          <span>Steepest climb</span>
          <span></span>
          <span class="value">{{ num(steepest, 1) }} %</span>
        </template>
      </div>
      <ElevationProfile :profile="profile" @hover="emit('hover', $event)" />
      <div class="footer">
        <button type="button" class="link" @click="downloadGpx(trip, name)">Download GPX</button>
      </div>
    </div>
  </section>
</template>

<style scoped>
.panel {
  display: grid;
  gap: 6px;
  width: min(640px, 100%);
  box-sizing: border-box;
  padding: 8px 12px 10px;
  border-radius: var(--radius);
  background: var(--surface);
  box-shadow: 0 2px 10px rgb(23 33 43 / 0.18);
  font-size: 13px;
  font-variant-numeric: tabular-nums;
}

.top {
  display: flex;
  align-items: center;
  gap: 8px;
}

/* Scroll route tabs horizontally instead of wrapping many variants. */
.tabs {
  display: flex;
  flex: 1;
  gap: 4px;
  min-width: 0;
  overflow-x: auto;
}

.tab {
  display: inline-flex;
  flex: none;
  align-items: center;
  gap: 6px;
  padding: 4px 9px;
  border: 1px solid var(--line);
  border-radius: 999px;
  background: var(--surface);
  color: var(--muted);
  cursor: pointer;
}

.tab[aria-selected='true'] {
  border-color: var(--tab);
  color: var(--ink);
  font-weight: 600;
}

.swatch {
  width: 14px;
  height: 4px;
  border-radius: 2px;
  background: var(--tab);
}

.miss {
  color: var(--warn);
  font-weight: 700;
}

.toggle {
  flex: none;
  padding: 4px 8px;
  border: 0;
  border-radius: var(--radius);
  background: var(--wash);
  cursor: pointer;
}

.line {
  display: flex;
  flex-wrap: wrap;
  gap: 4px 14px;
  margin: 0;
  color: var(--muted);
}

.line strong {
  color: var(--ink);
}

.warning {
  color: var(--warn);
}

.note {
  margin: 0;
  color: var(--muted);
}

.details {
  display: grid;
  gap: 10px;
  padding-top: 8px;
  border-top: 1px solid var(--line);
}

.metrics {
  display: grid;
  grid-template-columns: auto 1fr auto;
  align-items: center;
  gap: 4px 10px;
}

.bar {
  height: 4px;
  border-radius: 2px;
  background: var(--wash);
  overflow: hidden;
}

.bar span {
  display: block;
  height: 100%;
  background: var(--route);
}

.value {
  text-align: right;
}

.footer {
  display: flex;
  align-items: baseline;
  justify-content: flex-end;
  gap: 12px;
}
</style>
