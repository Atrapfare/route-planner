<script setup lang="ts">
import type { Poi, Point } from '../api'
import { MAX_POIS } from '../config'
import PoiSearch from './PoiSearch.vue'

defineProps<{ near: Point }>() // search around map centre
const pois = defineModel<Poi[]>({ required: true })

function add(poi: Poi) {
  if (pois.value.length < MAX_POIS && !pois.value.some((p) => p.id === poi.id)) {
    pois.value = [...pois.value, poi]
  }
}

function remove(poi: Poi) {
  pois.value = pois.value.filter((p) => p !== poi)
}
</script>

<template>
  <div class="picker">
    <PoiSearch
      :near="near"
      placeholder="Search a place, e.g. Fernsehturm"
      label="Search a POI"
      :disabled="pois.length >= MAX_POIS"
      @choose="add"
    />

    <ol v-if="pois.length" class="chosen">
      <li v-for="(poi, i) in pois" :key="poi.id ?? poi.pos.join()">
        <span class="number">{{ i + 1 }}</span>
        <span class="name">{{ poi.name }}</span>
        <button type="button" class="remove" :aria-label="`Remove ${poi.name}`" @click="remove(poi)">Remove</button>
      </li>
    </ol>
  </div>
</template>

<style scoped>
.picker {
  display: grid;
  gap: 8px;
}

.chosen {
  display: grid;
  gap: 6px;
  list-style: none;
  margin: 0;
  padding: 0;
}

.chosen li {
  display: grid;
  grid-template-columns: 22px 1fr auto;
  align-items: center;
  gap: 8px;
}

.number {
  width: 22px;
  height: 22px;
  border-radius: 50%;
  background: var(--ink);
  color: var(--surface);
  font-size: 12px;
  font-weight: 600;
  line-height: 22px;
  text-align: center;
}

.name {
  min-width: 0;
  overflow-wrap: anywhere;
}

.remove {
  border: 0;
  padding: 2px 0;
  background: none;
  font-size: 13px;
  color: var(--muted);
  cursor: pointer;
}

.remove:hover {
  color: var(--warn);
}
</style>
