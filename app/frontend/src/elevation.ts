// Elevation profile, grade colouring, riding time and GPX export of a route.

import type { PathPoint, Trip } from './api'

// Grades are measured over at least 200 m. SRTM has ~90 m cells and often picks
// up the slope next to the road, so short sections would look far too steep.
const GRADE_WINDOW_M = 200
const MODERATE_GRADE_PCT = 3
const STEEP_GRADE_PCT = 6
// Riding time: constant speed on the flat plus extra time per metre of ascent.
const FLAT_SPEED_KMH = 20
const MIN_PER_100M_UP = 5

export interface ProfilePoint {
  d: number // metres from start
  ele: number // elevation in metres
  pos: PathPoint
}

export type Steepness = 'flat' | 'moderate' | 'steep'

export interface SteepnessSection {
  from: number // metres from start
  to: number
  level: Steepness
}

function distance(a: PathPoint, b: PathPoint): number {
  const rad = Math.PI / 180
  const dLat = (b[0] - a[0]) * rad
  const dLon = (b[1] - a[1]) * rad
  const h = Math.sin(dLat / 2) ** 2 + Math.cos(a[0] * rad) * Math.cos(b[0] * rad) * Math.sin(dLon / 2) ** 2
  return 2 * 6371000 * Math.asin(Math.sqrt(h))
}

export function buildProfile(path: PathPoint[]): ProfilePoint[] {
  let d = 0
  return path.map((pos, i) => {
    if (i > 0) {
      d += distance(path[i - 1]!, pos)
    }
    return { d, ele: pos[2], pos }
  })
}

// Maximum grade over at least GRADE_WINDOW_M; null for short routes.
export function steepestGrade(profile: ProfilePoint[]): number | null {
  let best: number | null = null
  for (let i = 0, j = 0; i < profile.length; i++) {
    while (j < profile.length && profile[j]!.d - profile[i]!.d < GRADE_WINDOW_M) {
      j++
    }
    if (j === profile.length) {
      break
    }
    const grade = (100 * (profile[j]!.ele - profile[i]!.ele)) / (profile[j]!.d - profile[i]!.d)
    best = Math.max(best ?? 0, grade)
  }
  return best
}

// Splits the profile into sections for the grade colours. The grade of a
// section is taken over GRADE_WINDOW_M around its middle (cut off at the ends).
// Downhill gets the same colour as uphill.
export function steepnessSections(profile: ProfilePoint[]): SteepnessSection[] {
  const sections: SteepnessSection[] = []
  const last = profile.length - 1
  for (let i = 0, lo = 0, hi = 1; i < last; i++) {
    const from = profile[i]!.d
    const to = profile[i + 1]!.d
    if (to <= from) {
      continue // duplicate point
    }
    const mid = (from + to) / 2
    // lo is at/before window start; hi at/after window end.
    while (lo < i && profile[lo + 1]!.d <= mid - GRADE_WINDOW_M / 2) {
      lo++
    }
    hi = Math.max(hi, i + 1)
    while (hi < last && profile[hi]!.d < mid + GRADE_WINDOW_M / 2) {
      hi++
    }
    const grade = (100 * Math.abs(profile[hi]!.ele - profile[lo]!.ele)) / (profile[hi]!.d - profile[lo]!.d)
    const level: Steepness = grade >= STEEP_GRADE_PCT ? 'steep' : grade >= MODERATE_GRADE_PCT ? 'moderate' : 'flat'
    const prev = sections.at(-1)
    if (prev?.level === level) {
      prev.to = to
    } else {
      sections.push({ from, to, level })
    }
  }
  return sections
}

export function ridingTime(lengthM: number, ascentM: number): string {
  const minutes = Math.round((lengthM / 1000 / FLAT_SPEED_KMH) * 60 + (ascentM / 100) * MIN_PER_100M_UP)
  const h = Math.floor(minutes / 60)
  return h > 0 ? `${h} h ${minutes % 60} min` : `${minutes} min`
}

export function toGpx(trip: Trip, name: string): string {
  const points = trip.path.map(([lat, lon, ele]) => `      <trkpt lat="${lat}" lon="${lon}"><ele>${ele}</ele></trkpt>`)
  return (
    '<?xml version="1.0" encoding="UTF-8"?>\n' +
    '<gpx version="1.1" creator="Round Trip Planner" xmlns="http://www.topografix.com/GPX/1/1">\n' +
    `  <trk>\n    <name>${name}</name>\n    <trkseg>\n${points.join('\n')}\n    </trkseg>\n  </trk>\n</gpx>\n`
  )
}

export function downloadGpx(trip: Trip, name: string) {
  const url = URL.createObjectURL(new Blob([toGpx(trip, name)], { type: 'application/gpx+xml' }))
  const a = Object.assign(document.createElement('a'), { href: url, download: `${name}.gpx` })
  a.click()
  URL.revokeObjectURL(url)
}
