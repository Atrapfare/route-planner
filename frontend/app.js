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

async function computeRoute() {
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
}

map.on('click', async function(e) {
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

    // first click, or after a completed route: set new source
    if (sourceNodeId === null || targetNodeId !== null) {
        if (sourceMarker) map.removeLayer(sourceMarker);
        if (routeLayer)   { map.removeLayer(routeLayer); routeLayer = null; }
        if (targetMarker) { map.removeLayer(targetMarker); targetMarker = null; }

        targetNodeId = null;
        document.getElementById('target-coords').textContent = 'Not set';
        document.getElementById('target-id').textContent = '';
        document.getElementById('route-section').style.display = 'none';

        sourceNodeId = data.id;
        sourceMarker = L.marker([data.lat, data.lon], {
            icon: L.divIcon({ className: '', html: '<div style="width:14px;height:14px;background:#4caf50;border:2px solid #fff;border-radius:50%;box-shadow:0 0 4px #000"></div>', iconAnchor:[7,7] })
        }).addTo(map);

        document.getElementById('source-coords').textContent = `${data.lat.toFixed(5)}, ${data.lon.toFixed(5)}`;
        document.getElementById('source-id').textContent = `Node ID: ${data.id}`;

        setStatus('Now select a target', 'waiting');

    } else {
        // second click: set target and compute route
        if (targetMarker) map.removeLayer(targetMarker);

        targetNodeId = data.id;
        targetMarker = L.marker([data.lat, data.lon], {
            icon: L.divIcon({ className: '', html: '<div style="width:14px;height:14px;background:#e94560;border:2px solid #fff;border-radius:50%;box-shadow:0 0 4px #000"></div>', iconAnchor:[7,7] })
        }).addTo(map);

        document.getElementById('target-coords').textContent = `${data.lat.toFixed(5)}, ${data.lon.toFixed(5)}`;
        document.getElementById('target-id').textContent = `Node ID: ${data.id}`;

        await computeRoute();
    }
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

    setStatus('Click on the map', 'idle');
});
