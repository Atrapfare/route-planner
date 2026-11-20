// Calls to the backend (Vite proxies /api, see vite.config.ts). Errors are
// thrown with a message that can be shown to the user as is.

import type { SliderValues } from './config'

export type Point = [number, number] // [latitude, longitude]
export type PathPoint = [number, number, number] // [latitude, longitude, elevation in metres]

export type Method = 'isochrone' | 'polygon'

// Stop from the search (with POI ID) or set on the map (id = null).
export interface Poi {
  id: number | null
  name: string
  category: string
  pos: Point
}

export interface Trip {
  length_m: number
  ascent_m: number
  overlap_pct: number // percentage of route ridden twice
  bike_pct: number
  nature_pct: number
  unpaved_pct: number
  curvy_deg_km: number
  cost_per_km: number // cost-model cost per km, in perceived metres
  within_tolerance: boolean // length within ±5 % of target
  path: PathPoint[]
}

export interface TripRequest {
  start: Point
  lengthKm: number
  variants: number
  method: Method
  localSearch: boolean
  sliders: SliderValues
  pois: Poi[]
}

export interface TripResult {
  routes: Trip[]
  time_ms: number
  pool_size: number // candidate count for selection
  archive_size: number // candidates found by local search
  poi_tour: boolean // direct tour through the stops was already > 5 % too long and is the only result
}

// Friendlier texts for the usual backend errors.
const MESSAGES: Record<string, string> = {
  'point is outside the map': 'This point is outside the map.',
  'via point is outside the map': 'A POI set on the map is outside the map.',
  'no round trip found': 'No round trip found for this start.',
}

async function get<T>(path: string, params: Record<string, string | number>): Promise<T> {
  const query = new URLSearchParams(Object.entries(params).map(([k, v]) => [k, String(v)]))
  let res: Response
  let data
  try {
    res = await fetch(`${path}?${query}`)
    data = await res.json()
  } catch {
    throw new Error('The backend does not respond. Is it running (node app/start)?')
  }
  if (!res.ok) {
    throw new Error(MESSAGES[data.error] ?? data.error ?? `Error ${res.status}`)
  }
  return data as T
}

export async function bounds(): Promise<[Point, Point]> {
  const data = await get<{ min: Point; max: Point }>('/api/bounds', {})
  return [data.min, data.max]
}

export async function nearest(pos: Point): Promise<Point> {
  const data = await get<{ pos: Point }>('/api/nearest', { lat: pos[0], lon: pos[1] })
  return data.pos
}

export function searchPois(q: string, near: Point): Promise<Poi[]> {
  return get<Poi[]>('/api/pois', { q, lat: near[0], lon: near[1], limit: 8 })
}

export function roundTrips(r: TripRequest): Promise<TripResult> {
  const params: Record<string, string | number> = {
    lat: r.start[0],
    lon: r.start[1],
    length_km: r.lengthKm,
    variants: r.variants,
    method: r.method,
    ls: r.localSearch ? 1 : 0,
    ...r.sliders,
  }
  const ids = r.pois.filter((p) => p.id !== null).map((p) => p.id)
  if (ids.length > 0) {
    params.poi = ids.join(',')
  }
  const points = r.pois.filter((p) => p.id === null)
  if (points.length > 0) {
    params.via = points.map((p) => `${p.pos[0]},${p.pos[1]}`).join(';')
  }
  return get<TripResult>('/api/round-route', params)
}
