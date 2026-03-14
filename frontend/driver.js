/* ═══════════════════════════════════════════════════════════════════
   Chennai Transit — Driver Dashboard Logic (driver.js)
   Bus-centric view: select your bus, see your route & passengers
   ═══════════════════════════════════════════════════════════════════ */

const API = '';
let allStops = [];
let allBuses = [];
let allTrips = [];
let allUsers = [];
let allAlerts = [];
let liveHeartbeats = [];  // live BPM readings from /api/alerts/heartbeat
let selectedBus = null; // bus_number string

// ── Auth check ───────────────────────────────────────────────────
(function checkAuth() {
    const role = sessionStorage.getItem('user_role');
    if (role !== 'driver') {
        window.location.href = '/app/login.html';
        return;
    }
    const name = sessionStorage.getItem('user_name') || 'Driver';
    document.getElementById('driver-badge').textContent = '👤 ' + name;
})();

// ── Init ─────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', async () => {
    await fetchAll();
    populateBusSelector();

    // Restore previously selected bus
    const saved = sessionStorage.getItem('selected_bus');
    if (saved && allBuses.find(b => b.bus_number === saved)) {
        selectedBus = saved;
        hideBusSelector();
        renderDashboard();
    } else {
        showBusSelector();
    }

    // Auto-refresh every 8s
    setInterval(async () => {
        await fetchAll();
        if (selectedBus) renderDashboard();
    }, 8000);
});

// ── API helper ───────────────────────────────────────────────────
async function api(path) {
    try {
        const r = await fetch(`${API}${path}`);
        if (!r.ok) return [];
        return await r.json();
    } catch { return []; }
}

// ── Fetch all data ───────────────────────────────────────────────
async function fetchAll() {
    const [stops, buses, trips, users, alerts, heartbeats] = await Promise.all([
        api('/api/stops/'),
        api('/api/simulate/buses'),
        api('/api/trips/'),
        api('/api/users/'),
        api('/api/alerts/'),
        api('/api/alerts/heartbeat'),
    ]);
    allStops = stops;
    allBuses = buses;
    allTrips = trips;
    allUsers = users;
    allAlerts = alerts;
    liveHeartbeats = heartbeats;
}

// ── Bus selector ─────────────────────────────────────────────────
function populateBusSelector() {
    const sel = document.getElementById('sel-bus-pick');
    if (allBuses.length === 0) {
        sel.innerHTML = '<option value="">No buses available</option>';
        return;
    }
    sel.innerHTML = allBuses.map(b =>
        `<option value="${b.bus_number}">🚌 Bus ${b.bus_number}  —  ${b.current_stop}</option>`
    ).join('');
}

function showBusSelector() {
    document.getElementById('bus-select-overlay').classList.remove('hidden');
    populateBusSelector();
}

function hideBusSelector() {
    document.getElementById('bus-select-overlay').classList.add('hidden');
}

function confirmBus() {
    const sel = document.getElementById('sel-bus-pick');
    const val = sel.value;
    if (!val) return;
    selectedBus = val;
    sessionStorage.setItem('selected_bus', val);
    hideBusSelector();
    renderDashboard();
}

// ── Main render ──────────────────────────────────────────────────
function renderDashboard() {
    const bus = allBuses.find(b => b.bus_number === selectedBus);
    if (!bus) {
        showBusSelector();
        return;
    }

    // Get names via stop lookup
    const srcStop = allStops.find(s => s.id === bus.source_id);
    const dstStop = allStops.find(s => s.id === bus.destination_id);
    const srcName = srcStop ? srcStop.name : `Stop ${bus.source_id}`;
    const dstName = dstStop ? dstStop.name : `Stop ${bus.destination_id}`;

    // Hero card
    document.getElementById('hero-number').textContent = bus.bus_number;
    document.getElementById('hero-route').textContent = `${srcName} → ${dstName}`;
    document.getElementById('hero-current').innerHTML =
        `Currently at <strong>${bus.current_stop}</strong>`;

    // Header
    document.getElementById('header-tick').textContent =
        `Bus ${bus.bus_number} · ${bus.current_stop}`;

    // Build route for this bus
    const route = buildRoute(bus.source_id, bus.destination_id);
    const currentIdx = route.indexOf(bus.current_stop_id);

    // Stops remaining
    const stopsRemaining = currentIdx >= 0 ? route.length - 1 - currentIdx : 0;
    document.getElementById('stat-stops-remaining').textContent = stopsRemaining;

    // Passenger stats
    const myTrips = allTrips.filter(t => t.assigned_bus_number === selectedBus);
    const waiting = myTrips.filter(t => t.status === 'waiting');
    const boarded = myTrips.filter(t => t.status === 'boarded');
    const completed = myTrips.filter(t => t.status === 'completed');

    document.getElementById('stat-waiting').textContent = waiting.length;
    document.getElementById('stat-boarded').textContent = boarded.length;
    document.getElementById('stat-completed').textContent = completed.length;

    // Route strip
    renderRouteStrip(route, currentIdx);

    // Passengers (with user info)
    renderPassengers(myTrips);

    // Health alerts for passengers on this bus
    renderAlerts(myTrips);
}

// ── Build route (list of stop IDs from source to destination) ────
function buildRoute(srcId, dstId) {
    const ids = allStops.map(s => s.id).sort((a, b) => a - b);
    if (srcId <= dstId) {
        return ids.filter(id => id >= srcId && id <= dstId);
    } else {
        const rev = ids.filter(id => id >= dstId && id <= srcId);
        rev.reverse();
        return rev;
    }
}

// ── Route progress strip ─────────────────────────────────────────
function renderRouteStrip(route, currentIdx) {
    const strip = document.getElementById('route-strip');

    if (route.length === 0) {
        strip.innerHTML = '<div class="empty-pax">Route unavailable</div>';
        return;
    }

    strip.innerHTML = route.map((stopId, i) => {
        const stop = allStops.find(s => s.id === stopId);
        const name = stop ? stop.name : `#${stopId}`;

        let dotClass = 'is-upcoming';
        let nameClass = '';
        if (i === currentIdx) {
            dotClass = 'is-current';
            nameClass = 'is-current';
        } else if (i < currentIdx) {
            dotClass = 'is-passed';
        }

        const connector = i < route.length - 1
            ? `<div class="route-connector ${i < currentIdx ? 'passed' : ''}"></div>`
            : '';

        return `
            <div class="route-stop">
                <div class="route-stop-dot ${dotClass}"></div>
                <div class="route-stop-name ${nameClass}">${name}</div>
            </div>
            ${connector}`;
    }).join('');
}

// ── Passengers list ──────────────────────────────────────────────
function renderPassengers(trips) {
    const container = document.getElementById('passenger-list');

    // Show active (waiting + boarded) first, then recently completed
    const active = trips.filter(t => t.status === 'waiting' || t.status === 'boarded');
    const recent = trips.filter(t => t.status === 'completed').slice(0, 5);
    const combined = [...active, ...recent];

    if (combined.length === 0) {
        container.innerHTML = '<div class="empty-pax">No passengers on this bus yet</div>';
        return;
    }

    container.innerHTML = combined.map(t => {
        const from = t.from_stop?.name || `Stop ${t.from_stop_id}`;
        const to = t.to_stop?.name || `Stop ${t.to_stop_id}`;
        const status = (t.status || '').toLowerCase();

        // Find user info
        const user = allUsers.find(u => u.id === t.user_id);
        const userName = user ? user.name : `User #${t.user_id}`;
        const userRfid = user ? user.rfid_number : '—';
        const userPhone = user?.guardian_phone || '';

        let badgeCls = 'badge-waiting';
        let icon = '⏳';
        if (status === 'boarded') { badgeCls = 'badge-boarded'; icon = '🚌'; }
        if (status === 'completed') { badgeCls = 'badge-completed'; icon = '✅'; }

        // Live heart rate badge (only for boarded passengers)
        let hrBadge = '';
        if (status === 'boarded' && user) {
            const hb = liveHeartbeats.find(h => h.rfid === user.rfid_number);
            if (hb) {
                const hrClass = `hr-${hb.status}`;
                const hrIcon = hb.status === 'critical' ? '❤️' : hb.status === 'warning' ? '🧡' : '💚';
                hrBadge = `
                    <div class="pax-heartrate ${hrClass}">
                        <span class="hr-icon">${hrIcon}</span>
                        <span class="hr-bpm">${hb.bpm}</span>
                        <span class="hr-unit">bpm</span>
                    </div>`;
            } else {
                hrBadge = `
                    <div class="pax-heartrate hr-pending">
                        <span class="hr-icon">🩶</span>
                        <span class="hr-bpm">—</span>
                        <span class="hr-unit">bpm</span>
                    </div>`;
            }
        }

        return `
            <div class="passenger-item">
                <div class="pax-info">
                    <div class="pax-name">${icon} ${userName}</div>
                    <div class="pax-route">${from} → ${to}</div>
                    <div class="pax-meta">RFID: ${userRfid} · Trip #${t.id}</div>
                </div>
                <div class="pax-right">
                    ${hrBadge}
                    <span class="badge ${badgeCls}">${t.status}</span>
                    ${userPhone ? `<span class="pax-phone">📞 ${userPhone}</span>` : ''}
                </div>
            </div>`;
    }).join('');
}

// ── Health alerts ────────────────────────────────────────────────
function renderAlerts(myTrips) {
    const container = document.getElementById('alert-list');

    // Only consider ACTIVE trips (waiting/boarded) — not completed
    const activeTrips = myTrips.filter(t => t.status === 'waiting' || t.status === 'boarded');

    // Get user IDs from active trips only
    const activeUserIds = new Set(activeTrips.map(t => t.user_id));
    // Get trip IDs from active trips only
    const activeTripIds = new Set(activeTrips.map(t => t.id));

    // ── LIVE heartbeat readings (from simulator or hardware) ──────────
    const liveCards = [];
    for (const trip of activeTrips) {
        if (trip.status !== 'boarded') continue;
        const user = allUsers.find(u => u.id === trip.user_id);
        if (!user) continue;

        const hb = liveHeartbeats.find(h => h.rfid === user.rfid_number);
        if (!hb) continue;

        const isAbnormal = hb.bpm > 100 || hb.bpm < 60;
        const isCritical = hb.bpm > 150 || hb.bpm < 40;
        const itemClass = isCritical ? '' : isAbnormal ? '' : 'alert-normal';
        const hrIcon = isCritical ? '❤️' : isAbnormal ? '🧡' : '💚';
        const statusLabel = isCritical ? '⚠️ CRITICAL' : isAbnormal ? '⚠️ Warning' : '● Normal';
        const timeStr = hb.updated_at ? new Date(hb.updated_at).toLocaleTimeString() : 'now';

        liveCards.push(`
            <div class="alert-item ${itemClass}">
                <div class="alert-info">
                    <div class="alert-user">${user.name}</div>
                    <div class="alert-meta">
                        RFID: ${user.rfid_number} · Bus ${hb.bus_number || trip.assigned_bus_number} · ${statusLabel} · ${timeStr}
                    </div>
                </div>
                <div class="alert-bpm">
                    <span class="heart-icon">${hrIcon}</span>
                    <div>
                        <div class="bpm-value">${hb.bpm}</div>
                        <div class="bpm-label">BPM</div>
                    </div>
                </div>
            </div>`);
    }

    // ── Historical DB alerts ─────────────────────────────────────────
    const relevantAlerts = allAlerts.filter(a =>
        (a.trip_id && activeTripIds.has(a.trip_id)) ||
        activeUserIds.has(a.user_id)
    );

    // Also check by rfid for alerts that have rfid (active passengers only)
    const activeRfids = new Set();
    for (const uid of activeUserIds) {
        const u = allUsers.find(x => x.id === uid);
        if (u) activeRfids.add(u.rfid_number);
    }
    const extraAlerts = allAlerts.filter(a =>
        a.rfid && activeRfids.has(a.rfid) && !relevantAlerts.includes(a)
    );
    const allRelevant = [...relevantAlerts, ...extraAlerts].slice(0, 20);

    const dbCards = allRelevant.map(a => {
        const isAbnormal = a.bpm > 100 || a.bpm < 60;
        const itemClass = isAbnormal ? '' : 'alert-normal';
        const heartColor = isAbnormal ? '❤️' : '💚';
        const timeStr = a.created_at ? new Date(a.created_at).toLocaleTimeString() : '';

        return `
            <div class="alert-item ${itemClass}">
                <div class="alert-info">
                    <div class="alert-user">${a.user_name || 'Unknown'}</div>
                    <div class="alert-meta">
                        ${a.rfid ? `RFID: ${a.rfid} · ` : ''}Trip #${a.trip_id || '—'} · ${timeStr}
                    </div>
                </div>
                <div class="alert-bpm">
                    <span class="heart-icon">${heartColor}</span>
                    <div>
                        <div class="bpm-value">${a.bpm}</div>
                        <div class="bpm-label">BPM</div>
                    </div>
                </div>
            </div>`;
    });

    const allCards = [...liveCards, ...dbCards];
    if (allCards.length === 0) {
        container.innerHTML = '<div class="empty-pax">No health alerts for your passengers</div>';
        return;
    }

    container.innerHTML = allCards.join('');
}

// ── Logout ───────────────────────────────────────────────────────
function logout() {
    sessionStorage.clear();
    window.location.href = '/app/login.html';
}
