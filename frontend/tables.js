/* ═══════════════════════════════════════════════════════════════════
   Chennai Transit — Database Viewer  (tables.js)
   Fetches all tables and renders them with auto-refresh
   ═══════════════════════════════════════════════════════════════════ */

const API = '';
let currentTab = 'stops';
let cache = {};

// ── Init ─────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
    // Tab click handlers
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            currentTab = btn.dataset.table;
            renderTable();
        });
    });

    refreshAll();
    setInterval(refreshAll, 8000);
});

// ── API helper ───────────────────────────────────────────────────
async function api(path) {
    try {
        const res = await fetch(`${API}${path}`);
        if (!res.ok) return [];
        return await res.json();
    } catch { return []; }
}

// ── Refresh all data ─────────────────────────────────────────────
async function refreshAll() {
    const [stops, buses, users, trips, alerts] = await Promise.all([
        api('/api/stops/'),
        api('/api/simulate/buses'),
        api('/api/users/'),
        api('/api/trips/'),
        api('/api/alerts/'),
    ]);

    cache = { stops, buses, users, trips, alerts };

    document.getElementById('cnt-stops').textContent = stops.length;
    document.getElementById('cnt-buses').textContent = buses.length;
    document.getElementById('cnt-users').textContent = users.length;
    document.getElementById('cnt-trips').textContent = trips.length;
    document.getElementById('cnt-alerts').textContent = alerts.length;
    document.getElementById('header-tick').textContent =
        `${buses.length} buses · ${users.length} users · ${trips.length} trips`;

    const now = new Date().toLocaleTimeString();
    document.getElementById('last-refreshed').textContent = `Last refreshed: ${now}`;

    renderTable();
}

// ── Status badge HTML ────────────────────────────────────────────
function badge(status) {
    const s = (status || '').toLowerCase();
    const cls =
        s === 'waiting' ? 'badge-waiting' :
            s === 'boarded' ? 'badge-boarded' :
                s === 'completed' ? 'badge-completed' :
                    s === 'running' ? 'badge-boarded' : '';
    return `<span class="badge ${cls}">${status}</span>`;
}

// ── Render current table ─────────────────────────────────────────
function renderTable() {
    const data = cache[currentTab] || [];
    const container = document.getElementById('table-content');

    if (data.length === 0) {
        container.innerHTML = `<div class="empty-msg">No ${currentTab} found</div>`;
        return;
    }

    let html = '';

    switch (currentTab) {
        case 'stops':
            html = tbl(
                ['ID', 'Name', 'Area', 'Latitude', 'Longitude'],
                data.map(s => [s.id, s.name, s.area, s.latitude?.toFixed(4), s.longitude?.toFixed(4)])
            );
            break;

        case 'buses':
            html = tbl(
                ['ID', 'Bus #', 'Current Stop', 'Source ID', 'Dest ID', 'Status'],
                data.map(b => [
                    b.id, `🚌 ${b.bus_number}`, b.current_stop,
                    b.source_id, b.destination_id, badge(b.status)
                ])
            );
            break;

        case 'users':
            html = tbl(
                ['ID', 'Name', 'RFID', 'Phone', 'Destination'],
                data.map(u => [
                    u.id, u.name, `<code>${u.rfid_number}</code>`,
                    u.guardian_phone || '—',
                    u.destination_stop ? u.destination_stop.name : '—',
                ])
            );
            break;

        case 'trips':
            html = tbl(
                ['ID', 'User ID', 'Bus #', 'From', 'To', 'Status'],
                data.map(t => [
                    t.id, t.user_id, t.assigned_bus_number,
                    t.from_stop?.name || t.from_stop_id,
                    t.to_stop?.name || t.to_stop_id,
                    badge(t.status),
                ])
            );
            break;

        case 'alerts':
            html = tbl(
                ['ID', 'User ID', 'Trip ID', 'BPM', 'SMS Sent', 'Created At'],
                data.map(a => [
                    a.id, a.user_id, a.trip_id ?? '—', a.bpm,
                    a.sms_sent ? '✅' : '❌',
                    a.created_at || '—',
                ])
            );
            break;
    }

    container.innerHTML = html;
}

// ── Table builder helper ─────────────────────────────────────────
function tbl(headers, rows) {
    return `
    <table>
      <thead><tr>${headers.map(h => `<th>${h}</th>`).join('')}</tr></thead>
      <tbody>${rows.map(r =>
        `<tr>${r.map(c => `<td>${c ?? ''}</td>`).join('')}</tr>`
    ).join('')}</tbody>
    </table>`;
}
