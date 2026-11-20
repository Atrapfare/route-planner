<script setup lang="ts">
import { computed } from 'vue'
import { SLIDERS, neutralSliders, type SliderValues } from '../config'
import NumberInput from './NumberInput.vue'

const values = defineModel<SliderValues>({ required: true })

const changed = computed(() => SLIDERS.some((s) => values.value[s.key] !== 50))
</script>

<template>
  <div class="sliders">
    <div v-for="s in SLIDERS" :key="s.key" class="slider">
      <div class="ends">
        <label :for="`slider-${s.key}`">{{ s.left }}</label>
        <span>{{ s.right }}</span>
      </div>
      <input
        :id="`slider-${s.key}`"
        v-model.number="values[s.key]"
        class="range centered"
        type="range"
        min="0"
        max="100"
        step="1"
        :aria-label="`${s.left} to ${s.right}`"
      />
      <NumberInput
        v-model="values[s.key]"
        class="number"
        :min="0"
        :max="100"
        :label="`${s.left} to ${s.right}, 0 to 100`"
      />
    </div>
    <button v-if="changed" type="button" class="link" @click="values = neutralSliders()">Reset all to neutral</button>
  </div>
</template>

<style scoped>
.sliders {
  display: grid;
  gap: 12px;
}

.slider {
  display: grid;
  grid-template-columns: 1fr auto;
  align-items: center;
  gap: 2px 12px;
}

.ends {
  display: flex;
  justify-content: space-between;
  font-size: 13px;
  color: var(--muted);
}

.number {
  grid-column: 2;
  grid-row: 1 / 3;
}

.link {
  justify-self: start;
}
</style>
