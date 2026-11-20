<script setup lang="ts">
import { ref } from 'vue'
import { searchPois, type Poi, type Point } from '../api'
import { poiLabel } from '../config'

const props = defineProps<{ near: Point; placeholder: string; label: string; disabled?: boolean }>()
const emit = defineEmits<{ choose: [poi: Poi] }>()

const query = ref('')
const hits = ref<Poi[]>([])
let timer: ReturnType<typeof setTimeout> | undefined

// Debounce typing to avoid a request per keystroke.
function onInput() {
  clearTimeout(timer)
  timer = setTimeout(search, 200)
}

async function search() {
  const q = query.value.trim()
  if (q.length < 2) {
    hits.value = []
    return
  }
  const found = await searchPois(q, props.near).catch(() => [])
  // Ignore stale responses if the field has changed since the request.
  if (query.value.trim() === q) {
    hits.value = found
  }
}

function choose(poi: Poi) {
  emit('choose', poi)
  query.value = ''
  hits.value = []
}
</script>

<template>
  <div class="search">
    <input
      v-model="query"
      type="search"
      class="field"
      :placeholder="placeholder"
      :aria-label="label"
      :disabled="disabled"
      @input="onInput"
    />
    <ul v-if="hits.length" class="hits">
      <li v-for="hit in hits" :key="hit.id ?? hit.name">
        <button type="button" @click="choose(hit)">
          <span class="name">{{ hit.name }}</span>
          <span class="category">{{ poiLabel(hit.category) }}</span>
        </button>
      </li>
    </ul>
  </div>
</template>

<style scoped>
.search {
  display: grid;
  gap: 8px;
}

.hits {
  margin: 0;
  padding: 0;
  list-style: none;
  border: 1px solid var(--line);
  border-radius: var(--radius);
  overflow: hidden;
}

.hits li + li {
  border-top: 1px solid var(--line);
}

.hits button {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  width: 100%;
  padding: 7px 10px;
  border: 0;
  background: var(--surface);
  text-align: left;
  cursor: pointer;
}

.hits button:hover {
  background: var(--wash);
}

.name {
  min-width: 0;
  overflow-wrap: anywhere;
}

.category {
  flex: none;
  font-size: 13px;
  color: var(--muted);
}
</style>
