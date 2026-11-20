<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import L from 'leaflet'
import type { Poi, Point, Trip } from '../api'
import { ROUTE_COLORS } from '../config'

const props = defineProps<{
  start: Point | null
  pois: Poi[]
  trips: Trip[]
  active: number
  hover: Point | null
  bounds: [Point, Point] | null
}>()

const emit = defineEmits<{
  pick: [pos: Point]
  moved: [center: Point]
  movePoi: [index: number, pos: Point]
}>()

const el = ref<HTMLElement | null>(null)
let map: L.Map
const markers = L.layerGroup()
const routes = L.layerGroup()
const arrows = L.layerGroup()
const ARROW_GAP_PX = 220 // screen pixels, not metres
const hoverMarker = L.circleMarker([0, 0], {
  radius: 6,
  color: '#ffffff',
  weight: 2,
  fillColor: '#17212b',
  fillOpacity: 1,
  interactive: false,
})

function drawMarkers() {
  markers.clearLayers()
  if (props.start) {
    const icon = L.divIcon({ className: 'pin pin-start', iconSize: [18, 18] })
    L.marker(props.start, { icon, interactive: false }).addTo(markers)
  }
  props.pois.forEach((poi, i) => {
    const icon = L.divIcon({ className: 'pin pin-poi', html: String(i + 1), iconSize: [22, 22] })
    L.marker(poi.pos, { icon, draggable: true, autoPan: true })
      .bindTooltip(poi.name, { direction: 'top', offset: [0, -10] })
      .on('dragend', (e) => {
        const { lat, lng } = (e.target as L.Marker).getLatLng()
        emit('movePoi', i, [lat, lng])
      })
      .addTo(markers)
  })
}

// White outline under the route so it stays visible on any background.
function drawRoute() {
  routes.clearLayers()
  const trip = props.trips[props.active]
  if (!trip) {
    return
  }
  L.polyline(trip.path, { color: '#ffffff', weight: 8, opacity: 0.9, interactive: false }).addTo(routes)
  L.polyline(trip.path, {
    color: ROUTE_COLORS[props.active % ROUTE_COLORS.length],
    weight: 4.5,
    interactive: false,
  }).addTo(routes)
}

// Direction arrows every ARROW_GAP_PX on screen, so zoomed out there are fewer
// of them.
function drawArrows() {
  arrows.clearLayers()
  const trip = props.trips[props.active]
  if (!trip) {
    return
  }
  const color = ROUTE_COLORS[props.active % ROUTE_COLORS.length]
  const pts = trip.path.map((p) => map.project(p))
  let next = ARROW_GAP_PX / 2
  let walked = 0
  for (let i = 1; i < pts.length; i++) {
    const a = pts[i - 1]!
    const b = pts[i]!
    const len = a.distanceTo(b)
    while (next <= walked + len) {
      const pos = map.unproject(a.add(b.subtract(a).multiplyBy((next - walked) / len)))
      const deg = (Math.atan2(b.y - a.y, b.x - a.x) * 180) / Math.PI
      const html = `<svg viewBox="0 0 16 16" style="color:${color};transform:rotate(${deg}deg)"><path d="M6 4l4 4-4 4"/></svg>`
      const icon = L.divIcon({ className: 'arrow', html, iconSize: [16, 16] })
      L.marker(pos, { icon, interactive: false, keyboard: false, zIndexOffset: -1000 }).addTo(arrows)
      next += ARROW_GAP_PX
    }
    walked += len
  }
}

// Fit the selected route, leaving room for the bottom results panel.
function fitRoute() {
  const trip = props.trips[props.active]
  if (trip) {
    map.fitBounds(L.latLngBounds(trip.path), { paddingTopLeft: [40, 60], paddingBottomRight: [40, 130] })
  }
}

onMounted(() => {
  map = L.map(el.value!, { zoomControl: false }).setView([48.66, 9.1], 10)
  L.control.zoom({ position: 'topright' }).addTo(map)
  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    attribution: '&copy; OpenStreetMap contributors',
    maxZoom: 19,
  }).addTo(map)
  routes.addTo(map)
  arrows.addTo(map)
  markers.addTo(map)

  map.on('click', (e) => emit('pick', [e.latlng.lat, e.latlng.lng]))
  map.on('zoomend', drawArrows)
  map.on('moveend', () => emit('moved', [map.getCenter().lat, map.getCenter().lng]))
  drawMarkers()
  drawRoute()
  drawArrows()
})

onBeforeUnmount(() => map.remove())

watch(
  () => props.hover,
  (pos) => {
    if (pos) {
      hoverMarker.setLatLng(pos).addTo(map)
    } else {
      hoverMarker.remove()
    }
  },
)
// Fit the graph region when first loaded, unless a start was already selected.
watch(
  () => props.bounds,
  (b) => {
    if (b && !props.start && props.trips.length === 0) {
      map.fitBounds(b)
    }
  },
)
// Zoom in on a start picked in the search if it is outside the view or the map
// is zoomed out too far. Only used for the search, map clicks keep the view.
function focus(pos: Point) {
  if (!map.getBounds().contains(pos) || map.getZoom() < 11) {
    map.setView(pos, 12)
  }
}
defineExpose({ focus })

watch(() => [props.start, props.pois], drawMarkers, { deep: true })
watch(() => [props.trips, props.active], drawRoute)
watch(() => [props.trips, props.active], fitRoute)
watch(() => [props.trips, props.active], drawArrows)
// Pan to newly added stops that are outside the view.
watch(
  () => props.pois.length,
  (now, before) => {
    const pos = props.pois[now - 1]?.pos
    if (now > before && pos && !map.getBounds().contains(pos)) {
      map.panTo(pos)
    }
  },
)
</script>

<template>
  <div ref="el" class="map"></div>
</template>

<style scoped>
.map {
  height: 100%;
  background: var(--wash);
}

/* Leaflet creates markers directly; style them with :deep(). */
.map :deep(.pin) {
  border-radius: 50%;
  box-shadow: 0 1px 4px rgb(23 33 43 / 0.35);
}

.map :deep(.pin-start) {
  background: var(--surface);
  border: 5px solid var(--ink);
  box-sizing: border-box;
}

.map :deep(.arrow) {
  border-radius: 50%;
  background: var(--surface);
  box-shadow: 0 0 0 1px rgb(23 33 43 / 0.25);
}

.map :deep(.arrow svg) {
  display: block;
  width: 100%;
  height: 100%;
  fill: none;
  stroke: currentColor;
  stroke-width: 2.4;
  stroke-linecap: round;
  stroke-linejoin: round;
}

.map :deep(.pin-poi) {
  cursor: grab;
  background: var(--ink);
  color: var(--surface);
  font: 600 12px/22px var(--font);
  text-align: center;
}
</style>
