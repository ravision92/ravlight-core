// RavLight SPA shell. Fetches /api/{features,status,config}, populates the form,
// delegates fixture-specific rendering to window.renderFixture (defined in
// /fixture.js — the server serves the matching file for the compiled fixture).
//
// All state lives in CFG. Save serialises form → JSON → POST /api/config and
// triggers a restart when the server flags one.

let F = {};       // feature flags
let CFG = {};     // current config (last seen from server)
let _statusTimer = null;

// ── Utilities ────────────────────────────────────────────────────────────────

function $(id) { return document.getElementById(id); }
function setVal(id, v) { const el = $(id); if (el) el.value = (v === undefined || v === null) ? '' : v; }
function getVal(id)    { const el = $(id); return el ? el.value : ''; }
function setChk(id, b) { const el = $(id); if (el) el.checked = !!b; }
function getChk(id)    { const el = $(id); return el ? el.checked : false; }

function formatRuntime(seconds) {
    const s = Number(seconds) || 0;
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    return h.toString().padStart(2,'0') + ':' + m.toString().padStart(2,'0');
}

function showToast(msg, ms) {
    const t = $('toast'); if (!t) return;
    t.textContent = msg;
    t.classList.add('show');
    setTimeout(() => t.classList.remove('show'), ms || 2500);
}

// ── Init ─────────────────────────────────────────────────────────────────────

async function init() {
    try {
        const [features, status, config] = await Promise.all([
            fetch('/api/features').then(r => r.json()),
            fetch('/api/status').then(r => r.json()),
            fetch('/api/config').then(r => r.json()),
        ]);
        F = features;
        CFG = config;

        // Hide elements whose data-mod feature is not compiled in.
        document.querySelectorAll('[data-mod]').forEach(el => {
            if (!F[el.dataset.mod]) el.remove();
        });

        applyStatus(status);
        applyConfig(config);

        if (typeof window.renderFixture === 'function') {
            window.renderFixture(config.fixture || {}, F);
        } else {
            $('fixtureSection').innerHTML = '<p class="field-note">No fixture renderer loaded.</p>';
        }

        // Periodic status refresh.
        _statusTimer = setInterval(refreshStatus, 2000);
    } catch (err) {
        console.error(err);
        showToast('Failed to load config: ' + err.message);
    }
}

function applyStatus(s) {
    $('hdrBoard').textContent        = s.board || '';
    $('hdrFw').textContent           = 'FW ' + (s.fw || '');
    $('fixtureName').textContent     = s.project || 'Fixture';
    $('titleId').textContent         = s.id || '';
    $('connMode').textContent        = s.mode || '';
    $('hdrRuntime').textContent      = formatRuntime(s.runtime);
    $('hdrTotalRuntime').textContent = formatRuntime(s.total_runtime);
    if (s.temp !== undefined) $('hdrTemp').textContent = (Number(s.temp).toFixed(1)) + '°C';
    document.title = 'RavLight ' + (s.id || '');
    $('fixHeader').textContent = (s.project || 'Fixture');

    // Network info rows
    const mdns = $('mdnsHost');
    if (mdns && s.id) {
        const host = 'rav' + s.id + '.local';
        mdns.textContent = host;
        mdns.href = 'http://' + host + '/';
    }
    const ipd = $('ipDisplay');
    if (ipd && s.ip) {
        ipd.textContent = s.ip;
        ipd.href = 'http://' + s.ip + '/';
    }

    // DMX status indicator
    const dot = $('dmxStatusDot');
    if (dot) dot.classList.toggle('live', !!s.dmx_active);

    // Mobile info popup mirrors
    const set = (id, v) => { const el = $(id); if (el && v !== undefined) el.textContent = v; };
    set('infoBoard',   s.board);
    set('infoFw',      s.fw);
    set('infoTemp',    s.temp !== undefined ? Number(s.temp).toFixed(1) + '°C' : undefined);
    set('infoCurrent', formatRuntime(s.runtime));
    set('infoTotal',   formatRuntime(s.total_runtime));
}

// Toggle the legacy accordion buttons (we keep their structure for visual parity
// even though .tabsec .acc-btn is hidden via CSS — needed for non-tab callers).
function toggleAcc(btn) {
    const wrap = btn.parentElement;
    const body = wrap.querySelector('.acc-body');
    const opening = !btn.classList.contains('open');
    btn.classList.toggle('open');
    body.classList.toggle('open');
    if (!body || !body.classList.contains('open')) {
        if (body) body.style.maxHeight = '0';
    } else if (body) {
        body.style.maxHeight = body.scrollHeight + 'px';
    }
}

function applyConfig(c) {
    const net = c.network || {};
    setVal('ssid',     net.ssid);
    setVal('password', net.password);
    setChk('dhcp',     net.dhcp);
    setVal('ip',       net.ip);
    setVal('subnet',   net.subnet);
    setVal('gateway',  net.gateway);

    const dmx = c.dmx || {};
    setVal('dmxInput',     dmx.input);
    setVal('dmxUniverse',  dmx.universe);
    if (F.dmxPhysical) setChk('dmxOutput', dmx.output);
    if (F.recorder)    setVal('autoSceneSlot', dmx.autoSceneSlot);

    setVal('ID_fixture', c.ID_fixture);
}

async function refreshStatus() {
    try {
        const s = await fetch('/api/status').then(r => r.json());
        // Only touch the runtime widgets — don't overwrite form fields.
        $('hdrRuntime').textContent      = formatRuntime(s.runtime);
        $('hdrTotalRuntime').textContent = formatRuntime(s.total_runtime);
        if (s.temp !== undefined) $('hdrTemp').textContent = Number(s.temp).toFixed(1) + '°C';
        const dot = $('dmxStatusDot');
        if (dot) dot.classList.toggle('active', !!s.dmx_active);
    } catch (e) { /* ignore transient errors */ }
}

// ── Tabs ─────────────────────────────────────────────────────────────────────

function showTab(name) {
    document.querySelectorAll('.tabsec').forEach(s => s.classList.toggle('active', s.id === 'tab-' + name));
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.toggle('active', b.dataset.tab === name));
}

// ── Save ─────────────────────────────────────────────────────────────────────

function buildPayload() {
    const payload = {
        ID_fixture: getVal('ID_fixture'),
        network: {
            ssid:     getVal('ssid'),
            password: getVal('password'),
            dhcp:     getChk('dhcp'),
            ip:       getVal('ip'),
            subnet:   getVal('subnet'),
            gateway:  getVal('gateway'),
        },
        dmx: {
            input:    parseInt(getVal('dmxInput')) || 0,
            universe: parseInt(getVal('dmxUniverse')) || 0,
        },
    };
    if (F.dmxPhysical) payload.dmx.output = getChk('dmxOutput');
    if (F.recorder)    payload.dmx.autoSceneSlot = parseInt(getVal('autoSceneSlot')) || 0;
    if (typeof window.getFixtureData === 'function') {
        const f = window.getFixtureData(F);
        if (f !== undefined && f !== null) payload.fixture = f;
    }
    return payload;
}

async function saveAll() {
    const btn = $('saveBtn');
    btn.disabled = true; btn.textContent = 'Saving…';
    try {
        const res = await fetch('/api/config', {
            method:  'POST',
            headers: {'Content-Type': 'application/json'},
            body:    JSON.stringify(buildPayload()),
        });
        const j = await res.json();
        if (j.ok) {
            if (j.restart_needed) restartDevice();
            else                  showToast('Saved');
        } else {
            showToast('Save failed: ' + (j.error || 'unknown'));
        }
    } catch (err) {
        showToast('Save error: ' + err.message);
    } finally {
        btn.disabled = false; btn.textContent = 'Save config';
    }
}

// ── Actions ─────────────────────────────────────────────────────────────────

async function restartDevice() {
    // Resolve the redirect target before kicking the restart so we can show it
    // in the overlay. Prefer the new mDNS host (in case the user just edited
    // ID_fixture) over the current IP.
    const newId = getVal('ID_fixture');
    const host  = (newId && newId.length) ? ('rav' + newId + '.local') : window.location.host;
    showRestartOverlay(host);
    try { await fetch('/restart', {method: 'POST'}); } catch (e) {}
}

function showRestartOverlay(host) {
    const overlay = $('restartOverlay');
    if (!overlay) return;
    const hostEl = $('restartHost');
    if (hostEl) hostEl.textContent = host;
    overlay.classList.add('open');
    let n = 10;
    const cd = $('restartCountdown');
    if (cd) cd.textContent = n;
    const t = setInterval(() => {
        n--;
        if (cd) cd.textContent = n;
        if (n <= 0) {
            clearInterval(t);
            location.href = 'http://' + host + '/';
        }
    }, 1000);
}

// Live-update the title fixture id and mDNS link as the user edits the ID field.
function updateMDNS() {
    const id = getVal('ID_fixture');
    const t = $('titleId'); if (t) t.textContent = id || '—';
    const m = $('mdnsHost');
    if (m && id) {
        const h = 'rav' + id + '.local';
        m.textContent = h;
        m.href = 'http://' + h + '/';
    }
}

function openResetModal()  { $('resetModal').classList.add('open'); }
function closeResetModal() { $('resetModal').classList.remove('open'); }

function toggleInfoPopup() { $('infoPopup').classList.toggle('open'); }

function toggleDevices() {
    const p = $('devicesPanel'); if (p) p.classList.toggle('open');
    const b = $('devicesBtn');   if (b) b.classList.toggle('active');
}

async function scanDevices() {
    const btn    = $('scanDevicesBtn');
    const status = $('scanStatus');
    const tbody  = document.querySelector('#deviceTable tbody');
    const espnow = ($('espnowScan') || {checked: false}).checked;
    if (btn) btn.disabled = true;
    if (status) status.textContent = espnow ? 'ESP-NOW scan…' : 'Scanning…';
    if (tbody) tbody.innerHTML = '<tr><td colspan="3" style="color:var(--txt4);text-align:center;padding:12px">Waiting for responses…</td></tr>';
    let info;
    try {
        info = await fetch(espnow ? '/discover?espnow=1' : '/discover').then(r => r.json());
    } catch (e) { if (status) status.textContent = 'Scan failed'; if (btn) btn.disabled = false; return; }
    const disrupted  = !!info.wifiDisrupted;
    const duration   = info.duration || 4500;
    const firstDelay = disrupted ? duration + 2000 : 1600;
    if (disrupted && status) status.textContent = 'WiFi suspended, scanning…';
    let pollCount = 0;
    const pollOnce = async () => {
        pollCount++;
        try {
            const devices = await fetch('/devices').then(r => r.json());
            if (devices.length > 0) renderDevices(devices);
            if (pollCount >= 3) {
                if (btn) btn.disabled = false;
                if (status) status.textContent = devices.length + ' device' + (devices.length === 1 ? '' : 's');
            } else {
                setTimeout(pollOnce, 1500);
            }
        } catch (e) {
            if (btn) btn.disabled = false;
            if (status) status.textContent = 'poll failed';
        }
    };
    setTimeout(pollOnce, firstDelay);
}

function renderDevices(devices) {
    const tbody = document.querySelector('#deviceTable tbody');
    if (!tbody) return;
    if (!devices.length) {
        tbody.innerHTML = '<tr><td colspan="3" style="color:var(--txt4);text-align:center;padding:12px">No devices found.</td></tr>';
        return;
    }
    tbody.innerHTML = '';
    devices.forEach(d => {
        const row = document.createElement('tr');
        row.innerHTML =
            '<td><b>' + (d.id || '—') + '</b></td>' +
            '<td><a href="http://' + d.ip + '" target="_blank" style="color:var(--txt3);text-decoration:none">' + d.ip + '</a></td>' +
            '<td><img src="/Iicon.png" style="width:18px;height:18px;cursor:pointer;opacity:.6" title="View Details"></td>';
        row.querySelector('img').addEventListener('click', e => { e.stopPropagation(); openDevicePopup(d); });
        tbody.appendChild(row);
    });
}

function openDevicePopup(d) {
    $('popupFixture').textContent = d.fixture || '—';
    $('popupId').textContent      = d.id || '—';
    $('popupMode').textContent    = d.mode || '—';
    $('popupIp').textContent      = d.ip || '—';
    $('popupMac').textContent     = d.mac || '—';
    $('popupFw').textContent      = d.fw || '—';
    $('popupTemp').textContent    = (d.temp > 0) ? d.temp.toFixed(1) + '°C' : '—';
    $('popupUptime').textContent  = formatUptime(d.uptime);
    const hw = d.hwMac || '', ip = d.ip || '';
    $('popupActions').innerHTML =
        '<button type="button" class="pop-btn" onclick="window.open(\'http://' + ip + '/\',\'_blank\')">Open UI &rarr;</button>' +
        '<button type="button" class="pop-btn" onclick="sendDeviceCmd(\'' + ip + '\',\'HIGHLIGHT\',\'' + hw + '\')">Highlight</button>' +
        '<button type="button" class="pop-btn danger" onclick="sendDeviceCmd(\'' + ip + '\',\'RESET\',\'' + hw + '\')">Reset config</button>';
    $('devicePopup').classList.add('open');
}

function closeDevicePopup() { $('devicePopup').classList.remove('open'); }

async function sendDeviceCmd(ip, cmd, hwMac) {
    const fd = new FormData();
    fd.append('ip', ip); fd.append('command', cmd);
    if (hwMac) fd.append('hwmac', hwMac);
    try {
        const r = await fetch('/device-cmd', {method: 'POST', body: fd});
        showToast(r.ok ? cmd + ' sent' : cmd + ' failed');
    } catch (e) { showToast(cmd + ' error'); }
}

function formatUptime(seconds) {
    const s = Number(seconds) || 0;
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    return h + 'h ' + m + 'm';
}

async function confirmReset() {
    closeResetModal();
    try { await fetch('/reset', {method: 'POST'}); } catch (e) {}
    showToast('Resetting to defaults…');
    setTimeout(() => location.reload(), 3000);
}

function openDmxMonitor() { window.open('/dmxmonitor', '_blank'); }

async function scanWifi() {
    showToast('Scanning…');
    try { await fetch('/scanWiFi'); } catch (e) {}
    let tries = 0;
    const poll = setInterval(async () => {
        tries++;
        try {
            const list = await fetch('/getWiFiList').then(r => r.json());
            if (list && list.length) {
                clearInterval(poll);
                const dl = $('wifi-networks');
                if (dl) {
                    dl.innerHTML = '';
                    list.forEach(ssid => { const o = document.createElement('option'); o.value = ssid; dl.appendChild(o); });
                }
                showToast(list.length + ' networks found');
            }
        } catch (e) { /* keep polling */ }
        if (tries > 6) clearInterval(poll);
    }, 1500);
}

// Configuration import/export
async function downloadJSON() {
    try {
        const cfg = await fetch('/api/config').then(r => r.json());
        const blob = new Blob([JSON.stringify(cfg, null, 2)], {type: 'application/json'});
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url; a.download = 'ravlight-' + (cfg.ID_fixture || 'config') + '.json';
        a.click();
        URL.revokeObjectURL(url);
    } catch (e) { showToast('Download failed'); }
}

async function uploadJSON() {
    const file = $('jsonUpload').files[0];
    if (!file) { showToast('Pick a .json file first'); return; }
    try {
        const text = await file.text();
        const cfg  = JSON.parse(text);
        const res  = await fetch('/api/config', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body:    JSON.stringify(cfg),
        });
        const j = await res.json();
        if (j.ok) {
            showToast('Imported');
            if (j.restart_needed) restartDevice();
            else                  setTimeout(() => location.reload(), 500);
        } else {
            showToast('Import failed');
        }
    } catch (e) { showToast('Invalid JSON'); }
}

async function startRecording() {
    const slot = parseInt(getVal('autoSceneSlot')) || 0;
    try {
        await fetch('/startRecording?scene=' + slot);
        showToast('Recording scene ' + (slot + 1));
    } catch (e) { showToast('Failed to start recording'); }
}

window.addEventListener('DOMContentLoaded', init);
