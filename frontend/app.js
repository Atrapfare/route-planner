const map = L.map('map').setView([51.1657, 10.4515], 6);

L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19,
    attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
}).addTo(map);

let sourceMarker = null;
let targetMarker = null;
let sourceNodeId = null;
let targetNodeId = null;
let routeLayer   = null;
let mode         = null; // 'source' | 'target' | null

function setStatus(text, type) {
    const el = document.getElementById('status');
    el.textContent = text;
    el.className = 'status-' + type;
}

function showModal(title, body, details = '') {
    document.getElementById('modal-title').textContent = title;
    document.getElementById('modal-body').textContent = body;
    document.getElementById('modal-details').innerHTML = details;
    document.getElementById('modal-overlay').classList.add('visible');
}

document.getElementById('modal-close').addEventListener('click', function() {
    document.getElementById('modal-overlay').classList.remove('visible');
});

function updateCalculateButton() {
    document.getElementById('btn-calculate').disabled = (sourceNodeId === null || targetNodeId === null);
}

function setMode(newMode) {
    mode = newMode;
    document.getElementById('btn-set-source').classList.toggle('active', mode === 'source');
    document.getElementById('btn-set-target').classList.toggle('active', mode === 'target');
    if (mode === 'source') setStatus('Click on the map to set source', 'waiting');
    else if (mode === 'target') setStatus('Click on the map to set target', 'waiting');
    else setStatus('Select an action', 'idle');
}

document.getElementById('btn-set-source').addEventListener('click', function() {
    setMode(mode === 'source' ? null : 'source');
});

document.getElementById('btn-set-target').addEventListener('click', function() {
    setMode(mode === 'target' ? null : 'target');
});

document.getElementById('btn-calculate').addEventListener('click', async function() {
    if (sourceNodeId === null || targetNodeId === null) return;

    setStatus('Computing route...', 'loading');

    const startTime = performance.now();
    const response = await fetch(`/api/route?src=${sourceNodeId}&trg=${targetNodeId}`);
    const data = await response.json();
    const elapsed = ((performance.now() - startTime) / 1000).toFixed(2);

    if (routeLayer) map.removeLayer(routeLayer);

    if (data.distance < 0) {
        setStatus('No route found', 'error');
        showModal(
            'No route found',
            'There is no connection between the selected source and target in the road graph.',
            `<div class="detail-row"><span class="detail-label">Source node</span><span class="detail-val">${sourceNodeId}</span></div>
             <div class="detail-row"><span class="detail-label">Target node</span><span class="detail-val">${targetNodeId}</span></div>
             <div class="detail-divider"></div>
             <div class="detail-section-title">Possible reasons</div>
             <div class="detail-bullet">Source and target are in separate graph components</div>
             <div class="detail-bullet">A one-way street or ferry separates the areas</div>
             <div class="detail-divider"></div>
             <div class="detail-tip">Try selecting different points in the same region.</div>`
        );
        return;
    }

    routeLayer = L.polyline(data.path, { color: '#1565c0', weight: 5, opacity: 0.9 }).addTo(map);
    map.fitBounds(routeLayer.getBounds(), { padding: [40, 40] });

    const distKm = (data.distance / 1000).toFixed(1);
    document.getElementById('stat-distance').textContent = distKm + ' km';
    document.getElementById('stat-nodes').textContent    = data.path.length + ' nodes';
    document.getElementById('stat-time').textContent     = elapsed + ' s';
    document.getElementById('route-section').style.display = 'block';

    setStatus('Route computed ✓', 'done');
});

map.on('click', async function(e) {
    if (mode === null) return;

    const lat = e.latlng.lat;
    const lon = e.latlng.lng;

    document.getElementById('coordinates').textContent = `${lat.toFixed(5)}, ${lon.toFixed(5)}`;
    setStatus('Finding nearest node...', 'loading');

    const response = await fetch(`/api/nearest?lat=${lat}&lon=${lon}`);
    const data = await response.json();

    if (data.error) {
        setStatus('Outside map bounds', 'error');
        showModal(
            'Outside map bounds',
            'The clicked point is outside the loaded road graph.',
            `<div class="detail-section-title">Valid bounds</div>
             <div class="detail-row"><span class="detail-label">Latitude</span><span class="detail-val">${data.minLat?.toFixed(2)}° – ${data.maxLat?.toFixed(2)}°</span></div>
             <div class="detail-row"><span class="detail-label">Longitude</span><span class="detail-val">${data.minLon?.toFixed(2)}° – ${data.maxLon?.toFixed(2)}°</span></div>`
        );
        return;
    }

    if (routeLayer) { map.removeLayer(routeLayer); routeLayer = null; document.getElementById('route-section').style.display = 'none'; }

    if (mode === 'source') {
        if (sourceMarker) map.removeLayer(sourceMarker);
        sourceNodeId = data.id;
        sourceMarker = L.marker([data.lat, data.lon], {
            icon: L.divIcon({ className: '', html: `<svg xmlns="http://www.w3.org/2000/svg" width="20" height="30" viewBox="0 0 28 40"><path d="M14 0C6.27 0 0 6.27 0 14c0 9.625 14 26 14 26S28 23.625 28 14C28 6.27 21.73 0 14 0z" fill="#4caf50"/><circle cx="14" cy="14" r="6" fill="#fff"/></svg>`, iconAnchor: [10, 30] })
        }).addTo(map);
        document.getElementById('source-coords').textContent = `${data.lat.toFixed(5)}, ${data.lon.toFixed(5)}`;
        document.getElementById('source-id').textContent = `Node ID: ${data.id}`;
        setStatus('Click on the map to set source', 'waiting');
    } else if (mode === 'target') {
        if (targetMarker) map.removeLayer(targetMarker);
        targetNodeId = data.id;
        targetMarker = L.marker([data.lat, data.lon], {
            icon: L.divIcon({ className: '', html: `<svg xmlns="http://www.w3.org/2000/svg" width="20" height="30" viewBox="0 0 28 40"><path d="M14 0C6.27 0 0 6.27 0 14c0 9.625 14 26 14 26S28 23.625 28 14C28 6.27 21.73 0 14 0z" fill="#e94560"/><circle cx="14" cy="14" r="6" fill="#fff"/></svg>`, iconAnchor: [10, 30] })
        }).addTo(map);
        document.getElementById('target-coords').textContent = `${data.lat.toFixed(5)}, ${data.lon.toFixed(5)}`;
        document.getElementById('target-id').textContent = `Node ID: ${data.id}`;
        setStatus('Click on the map to set target', 'waiting');
    }

    updateCalculateButton();
});

document.getElementById('btn-clear').addEventListener('click', function() {
    if (sourceMarker) map.removeLayer(sourceMarker);
    if (targetMarker) map.removeLayer(targetMarker);
    if (routeLayer)   map.removeLayer(routeLayer);

    sourceMarker = null;
    targetMarker = null;
    routeLayer   = null;
    sourceNodeId = null;
    targetNodeId = null;

    document.getElementById('coordinates').textContent   = '—';
    document.getElementById('source-coords').textContent = 'Not set';
    document.getElementById('source-id').textContent     = '';
    document.getElementById('target-coords').textContent = 'Not set';
    document.getElementById('target-id').textContent     = '';
    document.getElementById('route-section').style.display = 'none';

    setMode(null);
    updateCalculateButton();
});

updateCalculateButton();
