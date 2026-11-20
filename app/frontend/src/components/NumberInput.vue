<script setup lang="ts">
const props = defineProps<{ min: number; max: number; label: string }>()
const value = defineModel<number>({ required: true })

function commit(ev: Event) {
  const input = ev.target as HTMLInputElement
  const n = Number(input.value.replace(',', '.'))
  const valid = input.value.trim() !== '' && Number.isFinite(n)
  const next = valid ? Math.min(props.max, Math.max(props.min, n)) : value.value
  value.value = next
  input.value = String(next) // show clamped value even if unchanged
}
</script>

<template>
  <input
    class="number"
    type="text"
    inputmode="decimal"
    :value="value"
    :aria-label="label"
    @change="commit"
    @keydown.enter="($event.target as HTMLInputElement).blur()"
  />
</template>

<style scoped>
.number {
  width: 3.5rem;
  box-sizing: border-box;
  padding: 3px 6px;
  border: 1px solid var(--line);
  border-radius: var(--radius);
  background: var(--surface);
  text-align: right;
  font-variant-numeric: tabular-nums;
}
</style>
