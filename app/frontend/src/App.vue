<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import * as api from './api'
import type { Method, Poi, Point, TripResult } from './api'
import { MAX_POIS, MAX_VARIANTS, neutralSliders, num } from './config'
import MapView from './components/MapView.vue'
import NumberInput from './components/NumberInput.vue'
import PoiSearch from './components/PoiSearch.vue'
import PoiPicker from './components/PoiPicker.vue'
import PreferenceSliders from './components/PreferenceSliders.vue'
import TripPanel from './components/TripPanel.vue'

const start = ref<Point | null>(null)
const startName = ref('') // POI name when start was chosen via search
const lengthKm = ref(20)
const variants = ref(3)
const sliders = ref(neutralSliders())
const pois = ref<Poi[]>([])
const method = ref<Method>('isochrone')
const localSearch = ref(true)

const result = ref<TripResult | null>(null)
const active = ref(0) // selected route; only this one appears on map
const loading = ref(false)
const error = ref('')
const hover = ref<Point | null>(null) // position hovered in elevation profile

const clickMode = ref<'start' | 'poi'>('start')
const CLICK_MODES = [
  { value: 'start', label: 'Start' },
  { value: 'poi', label: 'POI' },
] as const

const mapView = ref<InstanceType<typeof MapView> | null>(null)
const mapCenter = ref<Point>([48.66, 9.1])
// Bounds of the loaded graph, for the initial map view.
const mapBounds = ref<[Point, Point] | null>(null)
onMounted(async () => {
  mapBounds.value = await api.bounds().catch(() => null)
})

const METHODS: { value: Method; label: string }[] = [
  { value: 'isochrone', label: 'Isochrone' },
  { value: 'polygon', label: 'Polygon' },
]

// Snap a map click or searched location to the nearest network node.
async function setStart(pos: Point, name = '') {
  error.value = ''
  try {
    start.value = await api.nearest(pos)
    startName.value = name
    result.value = null // the old routes belong to the old start
  } catch (e) {
    error.value = (e as Error).message
  }
}

async function addPoi(pos: Point) {
  error.value = ''
  if (pois.value.length >= MAX_POIS) {
    error.value = `At most ${MAX_POIS} POIs.`
    return
  }
  try {
    const snapped = await api.nearest(pos)
    const name = `Map point ${num(snapped[0], 4)}, ${num(snapped[1], 4)}`
    pois.value = [...pois.value, { id: null, name, category: 'map point', pos: snapped }]
  } catch (e) {
    error.value = (e as Error).message
  }
}

// A dragged POI is snapped again and becomes a map point.
async function movePoi(index: number, pos: Point) {
  error.value = ''
  try {
    const snapped = await api.nearest(pos)
    const name = `Map point ${num(snapped[0], 4)}, ${num(snapped[1], 4)}`
    pois.value = pois.value.map((p, i) => (i === index ? { id: null, name, category: 'map point', pos: snapped } : p))
  } catch (e) {
    error.value = (e as Error).message
    pois.value = [...pois.value] // puts the marker back
  }
}

function onMapClick(pos: Point) {
  if (clickMode.value === 'start') {
    setStart(pos)
  } else {
    addPoi(pos)
  }
}

async function compute() {
  if (!start.value) {
    return
  }
  loading.value = true
  error.value = ''
  try {
    result.value = await api.roundTrips({
      start: start.value,
      lengthKm: lengthKm.value,
      variants: variants.value,
      method: method.value,
      localSearch: localSearch.value,
      sliders: sliders.value,
      pois: pois.value,
    })
  } catch (e) {
    error.value = (e as Error).message
    result.value = null
  } finally {
    active.value = 0
    loading.value = false
  }
}

function reset() {
  start.value = null
  startName.value = ''
  lengthKm.value = 20
  variants.value = 3
  sliders.value = neutralSliders()
  pois.value = []
  method.value = 'isochrone'
  localSearch.value = true
  result.value = null
  active.value = 0
  error.value = ''
  hover.value = null
  clickMode.value = 'start'
}

function select(index: number) {
  active.value = index
  hover.value = null
}

const trips = computed(() => result.value?.routes ?? [])

const stats = computed(() => {
  const r = result.value
  if (!r) {
    return ''
  }
  const count = r.routes.length === 1 ? '1 route' : `${r.routes.length} routes`
  const fromSearch = r.archive_size > 0 ? `, ${r.archive_size} of them from the local search` : ''
  return `${count} in ${num(r.time_ms)} ms, chosen from ${r.pool_size} candidates${fromSearch}`
})
</script>

<template>
  <div class="layout">
    <aside class="panel">
      <div class="settings">
        <header>
          <h1>Round Trip Planner</h1>
          <p class="hint">Bike round trips that fit your length and preferences.</p>
        </header>

        <section>
          <h2>Trip</h2>
          <div class="row">
            <span>Start</span>
            <span v-if="start && startName" class="value">{{ startName }}</span>
            <span v-else-if="start" class="value">{{ num(start[0], 4) }}° N, {{ num(start[1], 4) }}° E</span>
            <span v-else class="value empty">Click the map or search a place</span>
          </div>
          <PoiSearch
            :near="mapCenter"
            placeholder="Search a start, e.g. Tübingen"
            label="Search a start"
            @choose="(poi) => (setStart(poi.pos, poi.name), mapView?.focus(poi.pos))"
          />
          <div class="row">
            <label for="length">Length</label>
            <span class="value">
              <NumberInput id="length" v-model="lengthKm" :min="1" :max="200" label="Length in km" /> km
            </span>
          </div>
          <div class="row">
            <span id="variants-label">Variants</span>
            <div class="stepper" role="group" aria-labelledby="variants-label">
              <button type="button" aria-label="One variant less" :disabled="variants <= 1" @click="variants--">
                −
              </button>
              <NumberInput v-model="variants" class="count" :min="1" :max="MAX_VARIANTS" label="Number of variants" />
              <button
                type="button"
                aria-label="One variant more"
                :disabled="variants >= MAX_VARIANTS"
                @click="variants++"
              >
                +
              </button>
            </div>
          </div>
        </section>

        <section>
          <h2>Preferences</h2>
          <PreferenceSliders v-model="sliders" />
        </section>

        <section>
          <h2>POIs</h2>
          <p class="hint">
            Places every round trip passes. Search them (matches near the map centre come first) or switch the map to
            “POI” and click. Drag a POI on the map to move it.
          </p>
          <PoiPicker v-model="pois" :near="mapCenter" />
        </section>

        <section>
          <h2>Method</h2>
          <div class="segmented" role="radiogroup" aria-label="Method">
            <button
              v-for="m in METHODS"
              :key="m.value"
              type="button"
              role="radio"
              :aria-checked="method === m.value"
              @click="method = m.value"
            >
              {{ m.label }}
            </button>
          </div>
          <label class="check">
            <input v-model="localSearch" type="checkbox" />
            Improve the round trips with local search
          </label>
        </section>
      </div>

      <div class="action">
        <div class="buttons">
          <button type="button" class="primary" :disabled="!start || loading" @click="compute">
            {{ loading ? 'Computing …' : 'Compute round trips' }}
          </button>
          <button type="button" class="secondary" :disabled="loading" @click="reset">Reset</button>
        </div>
        <p v-if="error" class="error" role="alert">{{ error }}</p>
        <p v-else-if="!start" class="hint">Set the start on the map first.</p>
        <p v-else-if="stats" class="hint stats">{{ stats }}</p>
      </div>
    </aside>

    <main class="stage">
      <MapView
        ref="mapView"
        :start="start"
        :pois="pois"
        :trips="trips"
        :active="active"
        :hover="hover"
        :bounds="mapBounds"
        @pick="onMapClick"
        @moved="mapCenter = $event"
        @move-poi="movePoi"
      />
      <div class="segmented mode" role="radiogroup" aria-label="A click on the map sets">
        <span class="mode-label">Click sets</span>
        <button
          v-for="m in CLICK_MODES"
          :key="m.value"
          type="button"
          role="radio"
          :aria-checked="clickMode === m.value"
          @click="clickMode = m.value"
        >
          {{ m.label }}
        </button>
      </div>
      <div v-if="result && trips.length" class="results">
        <TripPanel :result="result" :active="active" @select="select" @hover="hover = $event" />
      </div>
    </main>
  </div>
</template>

<style scoped>
.layout {
  display: grid;
  grid-template-columns: 372px 1fr;
  height: 100%;
}

.panel {
  display: flex;
  flex-direction: column;
  min-height: 0;
  border-right: 1px solid var(--line);
}

.settings {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  padding: 22px 22px 8px;
}

h1 {
  margin: 0 0 4px;
  font-size: 21px;
  font-weight: 600;
  letter-spacing: -0.01em;
}

section {
  display: grid;
  gap: 10px;
  margin-top: 20px;
  padding-top: 18px;
  border-top: 1px solid var(--line);
}

h2 {
  margin: 0;
  font-size: 15px;
  font-weight: 600;
}

.row {
  display: grid;
  grid-template-columns: 4.5rem 1fr auto;
  align-items: center;
  gap: 10px;
  min-height: 28px;
}

.value {
  font-variant-numeric: tabular-nums;
}

.row > .value:nth-child(2) {
  grid-column: 2 / -1;
}

.value.empty {
  color: var(--muted);
}

.stepper {
  display: inline-grid;
  grid-template-columns: 28px auto 28px;
  align-items: center;
  justify-self: start;
  border: 1px solid var(--line);
  border-radius: var(--radius);
  text-align: center;
}

.stepper button {
  height: 28px;
  border: 0;
  background: none;
  font-size: 16px;
  cursor: pointer;
}

.stepper button:hover:not(:disabled) {
  background: var(--wash);
}

.stepper button:disabled {
  color: var(--line);
  cursor: default;
}

.segmented {
  display: grid;
  grid-template-columns: 1fr 1fr;
  padding: 3px;
  border-radius: var(--radius);
  background: var(--wash);
}

.segmented button {
  padding: 5px 0;
  border: 0;
  border-radius: 4px;
  background: none;
  color: var(--muted);
  cursor: pointer;
}

.segmented button[aria-checked='true'] {
  background: var(--surface);
  color: var(--ink);
  font-weight: 500;
  box-shadow: 0 0 0 1px var(--line);
}

.check {
  display: flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
}

.check input {
  width: 16px;
  height: 16px;
  margin: 0;
  accent-color: var(--ink);
}

.action {
  display: grid;
  gap: 8px;
  padding: 14px 22px 18px;
  border-top: 1px solid var(--line);
}

.primary {
  padding: 10px 14px;
  border: 0;
  border-radius: var(--radius);
  background: var(--ink);
  color: var(--surface);
  font-weight: 500;
  cursor: pointer;
}

.buttons {
  display: grid;
  grid-template-columns: 1fr auto;
  gap: 8px;
}

.secondary {
  padding: 10px 14px;
  border: 1px solid var(--line);
  border-radius: var(--radius);
  background: var(--surface);
  color: var(--ink);
  cursor: pointer;
}

.secondary:hover:not(:disabled) {
  background: var(--wash);
}

.secondary:disabled {
  color: var(--muted);
  cursor: default;
}

.primary:disabled {
  background: var(--line);
  color: var(--muted);
  cursor: default;
}

.stats {
  margin: 0;
  font-size: 12px;
}

.error {
  margin: 0;
  font-size: 13px;
  color: var(--warn);
}

.stage {
  position: relative;
  min-width: 0;
}

.results {
  position: absolute;
  left: 0;
  bottom: 0;
  z-index: 1000; /* above Leaflet layers */
  display: grid;
  justify-items: start;
  gap: 8px;
  max-width: 100%;
  box-sizing: border-box;
  padding: 16px;
  overflow-x: auto;
  pointer-events: none; /* keep the map interactive outside the results */
}

.results > * {
  pointer-events: auto;
}

.mode {
  position: absolute;
  top: 12px;
  left: 12px;
  z-index: 1000; /* above Leaflet layers */
  grid-template-columns: auto 1fr 1fr;
  align-items: center;
  gap: 2px;
  background: var(--surface);
  box-shadow: 0 1px 6px rgb(23 33 43 / 0.2);
}

.mode button {
  padding: 4px 12px;
}

.mode button[aria-checked='true'] {
  background: var(--ink);
  color: var(--surface);
  box-shadow: none;
}

.mode-label {
  padding: 0 6px 0 8px;
  font-size: 13px;
  color: var(--muted);
}

@media (max-width: 820px) {
  .layout {
    grid-template-columns: 1fr;
    grid-template-rows: minmax(0, 52%) minmax(0, 1fr);
  }

  .panel {
    border-right: 0;
    border-bottom: 1px solid var(--line);
  }
}
</style>
