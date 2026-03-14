/* ═══════════════════════════════════════════════════════════════════
   Chennai Transit — Home Dashboard  (home.js)
   Real-time overview: stats, bus map, activity feed, stop traffic
   ═══════════════════════════════════════════════════════════════════ */

const API = '';
let allStops = [];

document.addEventListener('DOMContentLoaded', async () => {
    await refresh();
    setInterval(refresh, 8000);
});

async function api(path) {
    try {
        const r = await fetch(`${API}${path}`);
        if (!r.ok) return [];
        return await r.json();
    } catch { return []; }
}

// ── Main refresh ─────────────────────────────────────────────────
async function refresh() {
    const [stops, buses, users, trips, alerts] = await Promise.all([
        api('/api/stops/'),
        api('/api/simulate/buses'),
        api('/api/users/'),
        api('/api/trips/'),
        api('/api/alerts/'),
    ]);

    allStops = stops;

    // ── Stat cards ────────────────────────────────────────────────
    animateValue('stat-buses', buses.length);
    animateValue('stat-trips', trips.length);
    animateValue('stat-users', users.length);
    animateValue('stat-alerts', alerts.length);

    const active = trips.filter(t => t.status === 'waiting' || t.status === 'boarded');
    const completed = trips.filter(t => t.status === 'completed');
    document.getElementById('stat-buses-sub').textContent = `Across ${stops.length} stops`;
    document.getElementById('stat-trips-sub').textContent =
        `${active.length} active · ${completed.length} completed`;
    document.getElementById('stat-users-sub').textContent =
        users.length === 0 ? 'No users yet' : `Latest: ${users[users.length - 1].name}`;
    document.getElementById('stat-alerts-sub').textContent =
        alerts.length === 0 ? 'No alerts' : `Latest BPM: ${alerts[alerts.length - 1]?.bpm || '—'}`;

    document.getElementById('header-tick').textContent =
        `Live · ${buses.length} buses · ${active.length} active trips`;

    // ── Live bus strip ────────────────────────────────────────────
    renderBusStrip(stops, buses);

    // ── Stop traffic chart ────────────────────────────────────────
    renderStopChart(stops, buses);

    // ── Activity feed ─────────────────────────────────────────────
    renderActivity(trips, users);
}

// ── Animated counter ─────────────────────────────────────────────
function animateValue(id, target) {
    const el = document.getElementById(id);
    const current = parseInt(el.textContent) || 0;
    if (current === target) return;
    el.textContent = target;
    el.style.transform = 'scale(1.15)';
    setTimeout(() => { el.style.transition = '0.3s'; el.style.transform = 'scale(1)'; }, 50);
}

// ── Bus strip ────────────────────────────────────────────────────
function renderBusStrip(stops, buses) {
    const stopBuses = {};
    for (const b of buses) {
        const sid = b.current_stop_id;
        if (!stopBuses[sid]) stopBuses[sid] = [];
        stopBuses[sid].push(b);
    }

    document.getElementById('live-strip').innerHTML = stops.map((s, i) => {
        const here = stopBuses[s.id] || [];
        const hasBus = here.length > 0;
        const names = here.map(b => b.bus_number).join(', ');
        const conn = i < stops.length - 1 ? '<div class="stop-connector"></div>' : '';
        return `
      <div class="stop-node">
        <div class="stop-dot ${hasBus ? 'has-bus' : ''}"></div>
        <div class="stop-name">${s.name}</div>
        <div class="stop-buses">${names}</div>
      </div>${conn}`;
    }).join('');

    document.getElementById('bus-list').innerHTML = buses.map(b =>
        `<div class="bus-chip">
      🚌 ${b.bus_number}
      <span class="bus-dir">@ ${b.current_stop} · ${b.source_id}→${b.destination_id}</span>
    </div>`
    ).join('');
}

// ── Stop traffic chart ───────────────────────────────────────────
function renderStopChart(stops, buses) {
    const counts = {};
    stops.forEach(s => counts[s.id] = 0);
    buses.forEach(b => { if (counts[b.current_stop_id] !== undefined) counts[b.current_stop_id]++; });

    const max = Math.max(...Object.values(counts), 1);

    document.getElementById('stop-chart').innerHTML = stops.map(s => {
        const c = counts[s.id] || 0;
        const pct = (c / max) * 100;
        return `
      <div class="stop-bar-row">
        <div class="stop-bar-label">${s.name}</div>
        <div class="stop-bar-track">
          <div class="stop-bar-fill" style="width:${pct}%"></div>
        </div>
        <div class="stop-bar-count">${c}</div>
      </div>`;
    }).join('');
}

// ── Activity feed ────────────────────────────────────────────────
function renderActivity(trips, users) {
    const userMap = {};
    users.forEach(u => userMap[u.id] = u.name);

    const feed = document.getElementById('activity-feed');

    if (trips.length === 0) {
        feed.innerHTML = '<div style="padding:20px;text-align:center;color:var(--text-muted)">No trips yet</div>';
        return;
    }

    // Show most recent 15 trips
    feed.innerHTML = trips.slice(0, 15).map(t => {
        const status = (t.status || '').toLowerCase();
        const user = userMap[t.user_id] || `User #${t.user_id}`;
        const from = t.from_stop?.name || `Stop ${t.from_stop_id}`;
        const to = t.to_stop?.name || `Stop ${t.to_stop_id}`;

        let verb = 'booked';
        if (status === 'boarded') verb = 'boarded bus';
        else if (status === 'completed') verb = 'completed trip';
        else if (status === 'waiting') verb = 'waiting for bus';

        return `
      <div class="activity-item">
        <div class="activity-dot ${status}"></div>
        <div class="activity-text">
          <strong>${user}</strong> ${verb} <strong>${t.assigned_bus_number}</strong><br>
          ${from} → ${to}
          <span class="badge badge-${status}">${t.status}</span>
          <div class="muted">Trip #${t.id}</div>
        </div>
      </div>`;
    }).join('');
}
