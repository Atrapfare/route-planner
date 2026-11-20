// Cost-model sliders (0–100; 50 neutral); key is the backend parameter name.
export const SLIDERS = [
  { key: 'hilly', left: 'Flat', right: 'Hilly' },
  { key: 'bike', left: 'Direct', right: 'Bike route' },
  { key: 'nature', left: 'Built-up', right: 'Nature' },
  { key: 'trail', left: 'Road', right: 'Trail' },
  { key: 'curvy', left: 'Straight', right: 'Curvy' },
] as const

export type SliderKey = (typeof SLIDERS)[number]['key']
export type SliderValues = Record<SliderKey, number>

export function neutralSliders(): SliderValues {
  return { hilly: 50, bike: 50, nature: 50, trail: 50, curvy: 50 }
}

// Okabe–Ito colours, readable for colour-blind users and visible on the map.
export const ROUTE_COLORS = ['#0072b2', '#d55e00', '#009e73', '#cc79a7', '#e69f00', '#56b4e9']

export function poiLabel(category: string): string {
  const text = category.replaceAll('_', ' ')
  return text.charAt(0).toUpperCase() + text.slice(1)
}

export const MAX_POIS = 10 // includes both POIs and map points
export const MAX_VARIANTS = 12

export function num(value: number, digits = 0): string {
  return value.toLocaleString('en-US', { minimumFractionDigits: digits, maximumFractionDigits: digits })
}
