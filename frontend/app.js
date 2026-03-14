/* ═══════════════════════════════════════════════════════════════════
   Chennai Transit — Simulation Dashboard  (app.js)
   All API interactions and live-update logic
   ═══════════════════════════════════════════════════════════════════ */

const API = '';

// ── State ────────────────────────────────────────────────────────
let allStops = [];
let currentUserId = null;
let currentUserRfid = null;

// ── Init ─────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', async () => {
    await loadStops();
    await refreshBuses();
    // Auto-refresh buses every 10s
    setInterval(refreshBuses, 10_000);
});

// ── Helpers ──────────────────────────────────────────────────────

async function api(path, opts = {}) {
    try {
        const res = await fetch(`${API}${path}`, {
            headers: { 'Content-Type': 'application/json' },
            ...opts,
        });
        const data = await res.json();
        if (!res.ok) throw new Error(data.detail || JSON.stringify(data));
        return data;
    } catch (e) {
        throw e;
    }
}

function show(elId, html, cls = 'info') {
    const el = document.getElementById(elId);
    el.innerHTML = `<div class="result-item ${cls}">${html}</div>`;
    el.scrollTop = 0;
}

function showMulti(elId, items, cls = 'info') {
    const el = document.getElementById(elId);
    el.innerHTML = items.map(h => `<div class="result-item ${cls}">${h}</div>`).join('');
    el.scrollTop = 0;
}

function badge(status) {
    const s = (status || '').toLowerCase();
    if (s === 'waiting') return `<span class="badge badge-waiting">Waiting</span>`;
    if (s === 'boarded') return `<span class="badge badge-boarded">Boarded</span>`;
    if (s === 'completed') return `<span class="badge badge-completed">Completed</span>`;
    if (s === 'running') return `<span class="badge badge-boarded">Running</span>`;
    return `<span class="badge">${status}</span>`;
}

function kv(label, value) {
    return `<span class="label">${label}:</span> <span class="value">${value}</span>`;
}

// ── Load Stops into all <select> elements ────────────────────────

async function loadStops() {
    try {
        allStops = await api('/api/stops/');
    } catch {
        allStops = [];
    }

    const selectors = [
        'sel-current-stop', 'sel-dest-stop',
        'sel-verify-stop', 'sel-sim-stop',
    ];

    for (const selId of selectors) {
        const sel = document.getElementById(selId);
        if (!sel) continue;
        sel.innerHTML = allStops.map(s =>
            `<option value="${s.id}">${s.name} (ID ${s.id})</option>`
        ).join('');
    }
}

// ═══════════════════════════════════════════════════════════════════
// LIVE BUS POSITIONS
// ═══════════════════════════════════════════════════════════════════

async function refreshBuses() {
    try {
        const buses = await api('/api/simulate/buses');

        // Build stop → buses map
        const stopBuses = {};
        for (const b of buses) {
            const sid = b.current_stop_id;
            if (!stopBuses[sid]) stopBuses[sid] = [];
            stopBuses[sid].push(b);
        }

        // Render strip
        const strip = document.getElementById('live-strip');
        strip.innerHTML = allStops.map((s, i) => {
            const busesHere = stopBuses[s.id] || [];
            const hasBus = busesHere.length > 0;
            const busLabels = busesHere.map(b => b.bus_number).join(', ');
            const connector = i < allStops.length - 1
                ? '<div class="stop-connector"></div>' : '';
            return `
        <div class="stop-node">
          <div class="stop-dot ${hasBus ? 'has-bus' : ''}"></div>
          <div class="stop-name">${s.name}</div>
          <div class="stop-buses">${busLabels}</div>
        </div>
        ${connector}
      `;
        }).join('');

        // Render chip list
        const list = document.getElementById('bus-list');
        list.innerHTML = buses.map(b =>
            `<div class="bus-chip">
        🚌 ${b.bus_number}
        <span class="bus-dir">@ ${b.current_stop} · ${b.source_id}→${b.destination_id}</span>
      </div>`
        ).join('');

        document.getElementById('header-tick').textContent =
            `Simulator Running · ${buses.length} buses`;
    } catch (e) {
        console.error('Bus refresh error:', e);
    }
}

// ═══════════════════════════════════════════════════════════════════
// PANEL 1: USER REGISTRATION / LOOKUP
// ═══════════════════════════════════════════════════════════════════

async function registerUser() {
    const name = document.getElementById('inp-name').value.trim();
    const rfid = document.getElementById('inp-rfid').value.trim();
    const phone = document.getElementById('inp-phone').value.trim();
    if (!name || !rfid) return show('result-user', 'Name and RFID required', 'error');

    try {
        const body = { name, rfid_number: rfid };
        if (phone) body.guardian_phone = phone;
        const u = await api('/api/users/', { method: 'POST', body: JSON.stringify(body) });
        currentUserId = u.id;
        currentUserRfid = u.rfid_number;
        fillTripFields();
        show('result-user', `
      ${kv('User ID', u.id)}<br>
      ${kv('Name', u.name)}<br>
      ${kv('RFID', u.rfid_number)}<br>
      ${kv('Phone', u.guardian_phone || '—')}
    `, 'success');
    } catch (e) {
        show('result-user', `Error: ${e.message}`, 'error');
    }
}

async function lookupUser() {
    const rfid = document.getElementById('inp-rfid').value.trim();
    if (!rfid) return show('result-user', 'Enter RFID to lookup', 'error');

    try {
        const u = await api(`/api/users/rfid/${rfid}`);
        currentUserId = u.id;
        currentUserRfid = u.rfid_number;
        fillTripFields();
        show('result-user', `
      ${kv('User ID', u.id)}<br>
      ${kv('Name', u.name)}<br>
      ${kv('RFID', u.rfid_number)}<br>
      ${kv('Destination', u.destination_stop ? u.destination_stop.name : '—')}
    `, 'success');
    } catch (e) {
        show('result-user', `Error: ${e.message}`, 'error');
    }
}

// ═══════════════════════════════════════════════════════════════════
// PANEL 2: FIND BUSES
// ═══════════════════════════════════════════════════════════════════

async function findNearestBus() {
    const cur = document.getElementById('sel-current-stop').value;
    const dst = document.getElementById('sel-dest-stop').value;
    if (cur === dst) return show('result-bus', 'Source and destination must differ', 'error');

    try {
        const b = await api(`/api/buses/nearest?current_stop_id=${cur}&destination_stop_id=${dst}`);
        // Auto-fill trip panel
        document.getElementById('inp-trip-bus').value = b.bus_number;
        document.getElementById('inp-trip-from').value = cur;
        document.getElementById('inp-trip-to').value = dst;

        show('result-bus', `
      ${kv('Nearest Bus', `🚌 ${b.bus_number}`)}<br>
      ${kv('Currently at', b.current_stop.name)}<br>
      ${kv('Route', `${b.source.name} → ${b.destination.name}`)}<br>
      ${badge(b.status)}
    `, 'success');
    } catch (e) {
        show('result-bus', `Error: ${e.message}`, 'error');
    }
}

async function findAllBuses() {
    const cur = document.getElementById('sel-current-stop').value;
    const dst = document.getElementById('sel-dest-stop').value;
    if (cur === dst) return show('result-bus', 'Source and destination must differ', 'error');

    try {
        const buses = await api(`/api/buses/?current_stop_id=${cur}&destination_stop_id=${dst}`);
        if (buses.length === 0) return show('result-bus', 'No eligible buses found', 'warn');

        showMulti('result-bus', buses.map(b => `
      ${kv('Bus', `🚌 ${b.bus_number}`)}
      · ${kv('At', b.current_stop.name)}
      · ${kv('Route', `${b.source.name} → ${b.destination.name}`)}
      ${badge(b.status)}
    `), 'info');
    } catch (e) {
        show('result-bus', `Error: ${e.message}`, 'error');
    }
}

// ═══════════════════════════════════════════════════════════════════
// PANEL 3: CREATE TRIP
// ═══════════════════════════════════════════════════════════════════

function fillTripFields() {
    if (currentUserId) document.getElementById('inp-trip-uid').value = currentUserId;
    if (currentUserRfid) {
        document.getElementById('inp-verify-rfid').value = currentUserRfid;
        document.getElementById('inp-alert-rfid').value = currentUserRfid;
    }
}

async function createTrip() {
    const user_id = parseInt(document.getElementById('inp-trip-uid').value);
    let bus = document.getElementById('inp-trip-bus').value.trim();
    const from_stop_id = parseInt(document.getElementById('inp-trip-from').value);
    const to_stop_id = parseInt(document.getElementById('inp-trip-to').value);

    if (!user_id || !from_stop_id || !to_stop_id) {
        return show('result-trip', 'User ID and stop IDs are required', 'error');
    }
    if (from_stop_id === to_stop_id) {
        return show('result-trip', 'Source and destination must differ', 'error');
    }

    // Auto-assign bus if not manually entered
    if (!bus) {
        try {
            show('result-trip', '🔍 Finding nearest bus...', 'info');
            const nearest = await api(`/api/buses/nearest?current_stop_id=${from_stop_id}&destination_stop_id=${to_stop_id}`);
            bus = nearest.bus_number;
            document.getElementById('inp-trip-bus').value = bus;
            show('result-trip', `🚌 Auto-assigned bus ${bus} (${nearest.stops_away} stops away). Creating trip...`, 'info');
        } catch (e) {
            return show('result-trip', `No bus found for this route: ${e.message}`, 'error');
        }
    }

    try {
        const t = await api('/api/trips/', {
            method: 'POST',
            body: JSON.stringify({
                user_id,
                assigned_bus_number: bus,
                from_stop_id,
                to_stop_id,
            }),
        });
        show('result-trip', `
      ${kv('Trip ID', t.id)}<br>
      ${kv('Bus', t.assigned_bus_number)}<br>
      ${kv('From', t.from_stop.name)} → ${kv('To', t.to_stop.name)}<br>
      ${badge(t.status)}
    `, 'success');
    } catch (e) {
        show('result-trip', `Error: ${e.message}`, 'error');
    }
}

// ═══════════════════════════════════════════════════════════════════
// PANEL 4: VERIFY TRIP
// ═══════════════════════════════════════════════════════════════════

async function verifyTrip() {
    const rfid = document.getElementById('inp-verify-rfid').value.trim();
    const stop = document.getElementById('sel-verify-stop').value;
    if (!rfid) return show('result-verify', 'Enter RFID', 'error');

    try {
        const r = await api(`/api/trips/verify/${rfid}`, {
            method: 'POST',
            body: JSON.stringify({ current_stop_id: parseInt(stop) }),
        });

        const status = r.status || '';
        let cls = 'info';
        if (status === 'BOARDED') cls = 'success';
        else if (status === 'COMPLETED') cls = 'success';
        else if (status === 'BUS_NOT_HERE') cls = 'warn';

        const lines = [kv('Status', badge(status)), `<br>${kv('Message', r.message)}`];
        if (r.bus_number) lines.push(`<br>${kv('Bus', r.bus_number)}`);
        if (r.destination) lines.push(`<br>${kv('Destination', r.destination)}`);
        if (r.bus_at) lines.push(`<br>${kv('Bus At', r.bus_at)}`);
        if (r.tapped_at) lines.push(`<br>${kv('Tapped At', r.tapped_at)}`);
        if (r.stops_remaining !== undefined) lines.push(`<br>${kv('Stops Remaining', r.stops_remaining)}`);

        show('result-verify', lines.join(''), cls);
    } catch (e) {
        show('result-verify', `Error: ${e.message}`, 'error');
    }
}

async function checkActiveTrip() {
    const rfid = document.getElementById('inp-verify-rfid').value.trim();
    if (!rfid) return show('result-verify', 'Enter RFID', 'error');

    try {
        const t = await api(`/api/trips/active/${rfid}`);
        show('result-verify', `
      ${kv('Trip ID', t.id)}<br>
      ${kv('Bus', t.assigned_bus_number)}<br>
      ${kv('From', t.from_stop.name)} → ${kv('To', t.to_stop.name)}<br>
      ${badge(t.status)}
    `, 'info');
    } catch (e) {
        show('result-verify', `Error: ${e.message}`, 'error');
    }
}

// ═══════════════════════════════════════════════════════════════════
// PANEL 5: SIMULATION CONTROLS
// ═══════════════════════════════════════════════════════════════════

async function simulateArrive() {
    const bus = document.getElementById('inp-sim-bus').value.trim();
    const stop = document.getElementById('sel-sim-stop').value;
    if (!bus) return show('result-sim', 'Enter bus number', 'error');

    try {
        const r = await api(`/api/simulate/arrive?bus_number=${encodeURIComponent(bus)}&stop_id=${stop}`);
        show('result-sim', `
      ${kv('Message', r.message)}<br>
      ${kv('Bus', r.bus_number)} → ${kv('Stop', r.stop_name)}
    `, 'success');
        // Refresh map
        setTimeout(refreshBuses, 300);
    } catch (e) {
        show('result-sim', `Error: ${e.message}`, 'error');
    }
}

// ═══════════════════════════════════════════════════════════════════
// PANEL 6: HEALTH ALERTS
// ═══════════════════════════════════════════════════════════════════

async function sendAlert() {
    const rfid = document.getElementById('inp-alert-rfid').value.trim();
    const bpm = parseInt(document.getElementById('inp-alert-bpm').value);
    if (!rfid || !bpm) return show('result-alerts', 'RFID and BPM required', 'error');

    try {
        const r = await api('/api/alerts/', {
            method: 'POST',
            body: JSON.stringify({ rfid, bpm, sms_sent: 0 }),
        });
        show('result-alerts', `
      ⚠️ ${kv('Alert ID', r.id)}<br>
      ${kv('User', r.user)}<br>
      ${kv('BPM', r.bpm)}<br>
      ${kv('Trip', r.trip_id || '—')}
    `, 'error');
    } catch (e) {
        show('result-alerts', `Error: ${e.message}`, 'error');
    }
}

async function viewAlerts() {
    try {
        const alerts = await api('/api/alerts/');
        if (alerts.length === 0) return show('result-alerts', 'No alerts', 'info');

        showMulti('result-alerts', alerts.map(a => `
      ${kv('ID', a.id)} · ${kv('User', a.user_name)} · ${kv('BPM', a.bpm)} · ${kv('Trip', a.trip_id || '—')}<br>
      <span class="label">${a.created_at}</span>
    `), 'warn');
    } catch (e) {
        show('result-alerts', `Error: ${e.message}`, 'error');
    }
}
