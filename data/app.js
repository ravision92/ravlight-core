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

// Format current uptime: seconds → "HH:mm". For sub-hour uptimes this still
// shows the leading "00:" — the colon makes it clear it's an HH:mm reading
// even at low values like "00:42".
function formatUptimeHHMM(seconds) {
    const s = Number(seconds) || 0;
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    return h.toString().padStart(2,'0') + ':' + m.toString().padStart(2,'0');
}

// Format total operating hours (integer, no minutes) → "Nh".
function formatTotalHours(hours) {
    const h = Number(hours) || 0;
    return h + 'h';
}

// RSSI (dBm) → human-readable signal strength: bars + dBm.
function formatRssi(dbm) {
    const v = Number(dbm) || 0;
    if (v === 0) return '—';
    let bars;
    if      (v >= -55) bars = '▂▄▆█';
    else if (v >= -65) bars = '▂▄▆_';
    else if (v >= -75) bars = '▂▄__';
    else if (v >= -85) bars = '▂___';
    else               bars = '____';
    return bars + ' ' + v + ' dBm';
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

        // Periodic status refresh. Skip when tab is hidden to keep background
        // load off the ESP32 webserver — every fetch opens a new TCP socket.
        _statusTimer = setInterval(() => {
            if (document.visibilityState === 'visible') refreshStatus();
        }, 10000);

        otaRefresh();
    } catch (err) {
        console.error(err);
        showToast('Failed to load config: ' + err.message);
    }
}

function applyStatus(s) {
    // hdrBoard was removed — board name lives only in the Info popup now
    $('hdrFw').textContent           = 'FW ' + (s.fw || '');
    // Installed version in the Firmware popup comes from /api/status (always
    // available) so it shows even when the OTA feed can't be reached.
    if (s.fw) { const oc = $('otaCurrent'); if (oc) oc.textContent = s.fw; }
    $('fixtureName').textContent     = s.project || 'Fixture';
    $('titleId').textContent         = s.id || '';
    $('connMode').textContent        = s.mode || '';
    // hdrRuntime / hdrTotalRuntime removed from the header — both live
    // in the Info popup now, accessible via the "i" button on every
    // viewport.
    if (s.temp !== undefined) $('hdrTemp').textContent = (Number(s.temp).toFixed(1)) + '°C';
    document.title = ((s.project || 'RavLight') + ' ' + (s.id || '')).trim();
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

    // DMX status indicator — yellow when DMX is receiving, green when the
    // device is reachable but no DMX traffic, gray on fetch failure.
    setDmxDot(s.dmx_active ? 'dmx' : 'idle');

    // Mobile info popup mirrors. Mode → connection type + RSSI when on WiFi,
    // plain "ETH" / "AP-WiFi" when not. Hours-only total runtime ("3h", "847h").
    const set = (id, v) => { const el = $(id); if (el && v !== undefined) el.textContent = v; };
    set('infoBoard',   s.board);
    set('infoFw',      s.fw);
    set('infoIp',      s.ip || '—');
    set('infoMdns',    s.mdns || ('rav' + (s.id || '') + '.local'));
    set('infoConn',    s.mode || '—');
    set('infoRssi',    (s.mode === 'WiFi' || s.mode === 'AP-WiFi') ? formatRssi(s.rssi) : '—');
    set('infoFps',     (s.fps !== undefined ? s.fps : 0) + ' fps');
    set('infoTemp',    s.temp !== undefined ? Number(s.temp).toFixed(1) + '°C' : undefined);
    set('infoCurrent', formatUptimeHHMM(s.uptime_sec));
    set('infoTotal',   formatTotalHours(s.total_hours));
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
    setChk('espnowEnable', net.espnow);

    const dmx = c.dmx || {};
    setVal('dmxInput',     dmx.input);
    setVal('dmxUniverse',  dmx.universe);
    if (F.dmxPhysical) setChk('dmxOutput', dmx.output);
    // out_offset slider lives in the Axon fixture panel, which is
    // rendered AFTER applyConfig, so set it lazily once the element
    // appears. requestAnimationFrame is enough — the fixture renderer
    // runs synchronously after applyConfig returns.
    if (dmx.out_offset !== undefined) {
        requestAnimationFrame(() => {
            const offEl = document.getElementById('axonOutOffset');
            const offLbl = document.getElementById('axonOutOffsetVal');
            if (offEl)  offEl.value = dmx.out_offset;
            if (offLbl) offLbl.textContent = dmx.out_offset;
        });
    }
    if (F.recorder)    setVal('autoSceneSlot', dmx.autoSceneSlot);

    if (F.effects) {
        const fx = dmx.effects || {};
        setVal('fxEffect',    fx.effect);
        setVal('fxSpeed',     fx.speed);
        setVal('fxIntensity', fx.intensity);
        setVal('fxWhite',       fx.white);
        setVal('fxStrobeRgb',   fx.strobe_rgb);
        setVal('fxStrobeWhite', fx.strobe_white);
        const fwl = document.getElementById('fxWhiteVal');
        if (fwl) fwl.textContent = (fx.white != null ? fx.white : 0);
        const fsr = document.getElementById('fxStrobeRgbVal');
        if (fsr) fsr.textContent = (fx.strobe_rgb != null ? fx.strobe_rgb : 0);
        const fsw = document.getElementById('fxStrobeWhiteVal');
        if (fsw) fsw.textContent = (fx.strobe_white != null ? fx.strobe_white : 0);
        // Veyron-only function block — show iff the firmware reports its
        // project name as "Veyron".
        const fv = document.getElementById('fxVeyronFunctions');
        if (fv) fv.style.display = (F.fixture === 'Veyron') ? '' : 'none';
        setChk('fxRgbw',      fx.rgbw);
        // Reflect numeric values in live readouts and color picker.
        const sv = document.getElementById('fxSpeedVal');
        if (sv) sv.textContent = (fx.speed != null ? fx.speed : 128);
        const iv = document.getElementById('fxIntensityVal');
        if (iv) iv.textContent = (fx.intensity != null ? fx.intensity : 255);
        const r = (fx.r != null ? fx.r : 255);
        const g = (fx.g != null ? fx.g : 0);
        const b = (fx.b != null ? fx.b : 0);
        rgbToPicker(r, g, b);
        if (typeof updateEffectsHint === 'function') updateEffectsHint();
    }

    setVal('ID_fixture', c.ID_fixture);
    // Show/hide the Effects controls panel based on the current DMX input.
    if (typeof updateEffectsPanel === 'function') updateEffectsPanel();
    updateDmxOutputGate();
}

// ── Effects helpers ────────────────────────────────────────────────────────
// Firmware now stores the base colour as raw R/G/B (was hue+intensity). The
// picker writes one hex per click, the UI parses it into three hidden fxR/G/B
// inputs that ride along in the /save payload.
function rgbToHex(r, g, b) {
    return '#' + (((1 << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF))
                 .toString(16)
                 .slice(1));
}
function hexToRgb(hex) {
    const v = hex.replace('#', '');
    return {
        r: parseInt(v.substr(0, 2), 16) || 0,
        g: parseInt(v.substr(2, 2), 16) || 0,
        b: parseInt(v.substr(4, 2), 16) || 0,
    };
}
function rgbToPicker(r, g, b) {
    const pk = document.getElementById('fxColor');
    if (pk) pk.value = rgbToHex(r, g, b);
    setVal('fxR', r);
    setVal('fxG', g);
    setVal('fxB', b);
    paintColorSwatchRgb(r, g, b);
}
function paintColorSwatchRgb(r, g, b) {
    const sw = document.getElementById('fxColorSwatch');
    if (!sw) return;
    const hex = rgbToHex(r, g, b);
    sw.style.background = hex;
    sw.style.boxShadow  = '0 0 12px ' + hex;
}
function onFxColor() {
    const pk = document.getElementById('fxColor');
    if (!pk) return;
    const { r, g, b } = hexToRgb(pk.value);
    setVal('fxR', r); setVal('fxG', g); setVal('fxB', b);
    paintColorSwatchRgb(r, g, b);
}
function updateEffectsHint() {
    const e = parseInt(getVal('fxEffect')) || 0;
    // Intensity slider is only meaningful for effects that don't take their
    // brightness from the picker. Solid/chase/twinkle render the picker RGB
    // directly; rainbow/fire use intensity as the overall brightness.
    const intensityRow = document.getElementById('fxIntensityRow');
    if (intensityRow) {
        const showIntensity = (e === 1 /*rainbow*/ || e === 3 /*fire*/);
        intensityRow.style.display = showIntensity ? '' : 'none';
    }
    const colourRow = document.getElementById('fxColorRow');
    if (colourRow) {
        const showColour = (e !== 1 /*rainbow*/ && e !== 3 /*fire*/);
        colourRow.style.display = showColour ? '' : 'none';
    }
    const h = document.getElementById('fxColorHint');
    if (h) {
        h.textContent = (e === 0) ? 'fill colour (brightness from picker)'
                      : (e === 2) ? 'head colour for chase'
                      : (e === 4) ? 'sparkle colour for twinkle'
                      :             '';
    }
}
// Expose to inline event handlers
window.onFxColor          = onFxColor;
window.updateEffectsHint  = updateEffectsHint;

// Hide the "DMX Node Output (ArtNet/sACN → RS-485)" toggle when the
// input is the wired RS-485 port (dmxInput == DMX_PHYSICAL = 1).
// Enabling both is a non-sensical combo (the device would simultaneously
// listen for incoming DMX on the wire AND attempt to transmit on it),
// and on auto-direction transceivers it has been observed to cause bus
// contention → frozen pixels + visible artifacts on whoever shares the
// cable. The wired-DMX-out wrapper for sendDmxData() already short-
// circuits in this case (it checks dmxInput != DMX_PHYSICAL), but the
// esp_dmx port stays installed in a TX-capable mode and the chip can
// briefly enter TX during init — UI lockout closes the loop properly.
function updateDmxOutputGate() {
    const inp = parseInt(document.getElementById('dmxInput')?.value) || 0;
    const row = document.getElementById('dmxOutputRow');
    if (!row) return;
    const isWired = (inp === 1);
    row.style.display = isWired ? 'none' : '';
    if (isWired) { const cb = document.getElementById('dmxOutput'); if (cb) cb.checked = false; }
}
window.updateDmxOutputGate = updateDmxOutputGate;

// Effects panel visibility: shown only when dmxInput == EFFECTS (5).
function updateEffectsPanel() {
    const sel = $('dmxInput');
    const fx  = $('effectsPanel');
    if (!sel || !fx) return;
    fx.style.display = (parseInt(sel.value) === 5) ? '' : 'none';
}

// Three-state DMX indicator:
//   'dmx'  — yellow, DMX traffic incoming on this universe
//   'idle' — green, device online but no DMX traffic
//   ''     — gray, device unreachable (fetch failed/timed out)
function setDmxDot(state) {
    const dot  = $('dmxStatusDot');
    const chip = $('dmxStatusChip');
    const lbl  = $('dmxStatusLbl');
    if (!dot) return;
    dot.classList.remove('dmx', 'idle');
    if (state) dot.classList.add(state);
    if (chip) {
        chip.title = state === 'dmx'  ? 'DMX traffic on this universe'
                   : state === 'idle' ? 'Device online — no DMX traffic'
                   :                    'Device offline';
    }
    if (lbl) {
        lbl.textContent = state === 'dmx'  ? 'DMX'
                        : state === 'idle' ? 'NO DMX'
                        :                    'OFFLINE';
    }
}

async function refreshStatus() {
    try {
        // 3 s abort budget so a hard-offline device flips the dot to gray
        // within ~5 s (matches the user request), instead of the browser's
        // default minute-long fetch timeout.
        const ac = new AbortController();
        const t  = setTimeout(() => ac.abort(), 3000);
        const s = await fetch('/api/status', {signal: ac.signal}).then(r => r.json());
        clearTimeout(t);
        // Header now only carries temp + DMX chip — runtime moved to the
        // Info popup. We still keep the popup contents fresh below.
        if (s.temp !== undefined) $('hdrTemp').textContent = Number(s.temp).toFixed(1) + '°C';
        setDmxDot(s.dmx_active ? 'dmx' : 'idle');
        // Live-refresh the Info popup body if it's open. Same set as the
        // initial applyConfig() population — kept in sync so the modal
        // doesn't go stale once it's been opened.
        const setIf = (id, v) => { const el = $(id); if (el && v !== undefined) el.textContent = v; };
        setIf('infoIp',      s.ip || '—');
        setIf('infoMdns',    s.mdns || ('rav' + (s.id || '') + '.local'));
        setIf('infoConn',    s.mode || '—');
        setIf('infoRssi',    (s.mode === 'WiFi' || s.mode === 'AP-WiFi') ? formatRssi(s.rssi) : '—');
        setIf('infoFps',     (s.fps !== undefined ? s.fps : 0) + ' fps');
        setIf('infoTemp',    s.temp !== undefined ? Number(s.temp).toFixed(1) + '°C' : undefined);
        setIf('infoCurrent', formatUptimeHHMM(s.uptime_sec));
        setIf('infoTotal',   formatTotalHours(s.total_hours));
        // Axon fixture panel — live FPS readout on the DMX OUT card.
        // The element only exists on Axon builds; setIf is a no-op
        // everywhere else.
        setIf('axonFps',     (s.fps !== undefined ? s.fps : 0));
    } catch (e) {
        // Fetch failed → device offline. Show gray (no class).
        setDmxDot('');
    }
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
            espnow:   getChk('espnowEnable'),
        },
        dmx: {
            input:    parseInt(getVal('dmxInput')) || 0,
            universe: parseInt(getVal('dmxUniverse')) || 0,
        },
    };
    if (F.dmxPhysical) payload.dmx.output = getChk('dmxOutput');
    // DMX OUT channel offset — rendered by the Axon fixture today, but
    // generic to any fixture that bridges to RS-485, so the payload
    // field is on dmx.out_offset (not under fixture).
    const offEl = document.getElementById('axonOutOffset');
    if (offEl) payload.dmx.out_offset = parseInt(offEl.value) || 0;
    if (F.recorder)    payload.dmx.autoSceneSlot = parseInt(getVal('autoSceneSlot')) || 0;
    if (F.effects) payload.dmx.effects = {
        effect:       parseInt(getVal('fxEffect'))      || 0,
        speed:        parseInt(getVal('fxSpeed'))       || 128,
        r:            parseInt(getVal('fxR'))           || 0,
        g:            parseInt(getVal('fxG'))           || 0,
        b:            parseInt(getVal('fxB'))           || 0,
        intensity:    parseInt(getVal('fxIntensity'))   || 255,
        rgbw:         getChk('fxRgbw') ? 1 : 0,
        white:        parseInt(getVal('fxWhite'))       || 0,
        strobe_rgb:   parseInt(getVal('fxStrobeRgb'))   || 0,
        strobe_white: parseInt(getVal('fxStrobeWhite')) || 0,
    };
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

// ── Firmware OTA (proprietary ravlight.com feed) ───────────────────────────
// Card states 1-4 live in the Settings accordion; the reboot + version
// verification (states 5-7) run in the full-screen #otaOverlay. Success is
// proven by re-reading the running version after the device comes back, not
// by the update request itself (which dies when the ESP32 reboots).
let _otaTimer    = null;    // polls /api/ota during check + download
let _otaVerify   = null;    // polls /api/status after reboot
let _otaVerifyT0 = 0;       // when verification started (for the 60s timeout)
let _otaTarget   = '';      // version we are updating to
let _otaUpdating = false;   // a manual upload was initiated from this page

function fetchJSON(url, ms, opts) {
    const ac = new AbortController();
    const t  = setTimeout(() => ac.abort(), ms || 6000);
    return fetch(url, Object.assign({signal: ac.signal}, opts || {}))
        .then(r => r.json()).finally(() => clearTimeout(t));
}

function otaApply(o) {
    if (!$('otaStatus')) return;   // OTA popup not in this build's DOM
    $('otaCurrent').textContent = o.current || '—';
    $('otaLatest').textContent  = o.available ? o.latest
                                : (o.checked ? o.current : (o.latest || '—'));
    $('otaLatest').style.color  = o.available ? 'var(--acc)' : '';

    const badge = $('otaBadge'), notes = $('otaNotesRow'), st = $('otaStatus'),
          dl = $('otaDownload'), ckBtn = $('otaCheckBtn');

    if (o.available && o.notes) { notes.innerHTML = '<b>What’s new:</b> ' + o.notes; notes.style.display = ''; }
    else notes.style.display = 'none';
    badge.style.display = o.available ? '' : 'none';

    // Mark the header FW badge so an update is visible without opening the popup.
    const hdr = $('hdrFw'); if (hdr) hdr.classList.toggle('has-update', !!o.available);

    // The download runs in the browser (not on the ESP32, which can't spare the
    // contiguous heap for a 1 MB HTTPS transfer). Show a link to the board's own
    // _fw_ image; the operator saves it and flashes it via "Upload firmware".
    if (o.available && o.url) {
        dl.href = o.url;
        dl.textContent = '⬇ Download v' + o.latest;
        dl.style.display = '';
    } else dl.style.display = 'none';

    st.className = 'ota-status'; ckBtn.disabled = false;
    if (o.error)          st.textContent = 'Update check failed (' + o.error + ')';
    else if (o.checking)  st.innerHTML   = '<span class="ota-spin"></span> &nbsp;Checking ravlight.com…';
    else if (o.available) st.innerHTML   = 'Version <b>' + o.latest + '</b> is available. Download it, then upload the file below.';
    else if (o.checked) { st.className = 'ota-status ok'; st.textContent = '✓ You are up to date.'; }
    else                  st.textContent = '—';

    // Poll only while a check is in flight; stop once idle.
    if (o.checking && !_otaTimer)  _otaTimer = setInterval(otaRefresh, 1500);
    if (!o.checking && _otaTimer) { clearInterval(_otaTimer); _otaTimer = null; }
}

async function otaRefresh() {
    try { otaApply(await fetchJSON('/api/ota', 6000)); } catch (e) {}
}

async function otaCheck() {
    const st = $('otaStatus'); st.className = 'ota-status';
    st.innerHTML = '<span class="ota-spin"></span> &nbsp;Checking ravlight.com…';
    try { await fetch('/api/ota/check', {method: 'POST'}); } catch (e) {}
    if (!_otaTimer) _otaTimer = setInterval(otaRefresh, 1500);
    otaRefresh();
}

// ── Reboot + version verification overlay (states 5-7) ─────────────────────
function otaBeginVerify() {
    if (_otaVerify) return;                       // already verifying
    if (_otaTimer) { clearInterval(_otaTimer); _otaTimer = null; }
    _otaUpdating = false;
    $('otaOvIcon').className   = 'ota-ring';
    $('otaOvIcon').textContent = '';
    $('otaOvTitle').textContent = 'Installing update…';
    $('otaOvMsg').innerHTML     = 'The device is rebooting.<br>Verifying the new version is active…';
    $('otaOvBtn').style.display = 'none';
    $('otaOverlay').classList.add('open');
    _otaVerifyT0 = Date.now();
    _otaVerify   = setInterval(otaVerifyPoll, 2000);
}

async function otaVerifyPoll() {
    // Let the device actually go down before the first probe.
    if (Date.now() - _otaVerifyT0 < 3000) return;
    try {
        const s = await fetchJSON('/api/status', 3000);
        clearInterval(_otaVerify); _otaVerify = null;
        // Pull update knows its target version → compare. A manual .bin upload
        // has no known target → any successful comeback counts as done.
        otaVerifyDone(_otaTarget ? (s.fw === _otaTarget) : true, s.fw);
    } catch (e) {
        if (Date.now() - _otaVerifyT0 > 60000) {  // gave up
            clearInterval(_otaVerify); _otaVerify = null;
            otaVerifyTimeout();
        } // else keep polling — device still down
    }
}

function otaVerifyDone(ok, fw) {
    $('otaOvIcon').className   = 'ota-icon ' + (ok ? 'ok' : 'err');
    $('otaOvIcon').textContent = ok ? '✓' : '!';
    if (ok) {
        $('otaOvTitle').textContent = 'Updated to v' + fw;
        $('otaOvMsg').textContent   = 'The device is back online on the new version.';
    } else {
        $('otaOvTitle').textContent = 'Update failed';
        $('otaOvMsg').innerHTML     = 'The device came back on <strong>v' + (fw || '?') +
            '</strong>.<br>The bootloader rolled back to the previous version.';
    }
    const b = $('otaOvBtn'); b.textContent = 'Continue'; b.style.display = '';
}

function otaVerifyTimeout() {
    $('otaOvIcon').className   = 'ota-icon err';
    $('otaOvIcon').textContent = '?';
    $('otaOvTitle').textContent = 'Couldn’t confirm';
    $('otaOvMsg').innerHTML     = 'The device didn’t come back within 60 s.<br>Reload the page to check its status.';
    const b = $('otaOvBtn'); b.textContent = 'Reload'; b.style.display = '';
}

// Reload to pull the (possibly new) embedded UI and refresh all state.
function otaOverlayClose() { location.reload(); }

// Firmware popup — opened from the header "FW x.xx" badge.
function toggleOtaPopup() {
    const p = $('otaPopup'); if (!p) return;
    const opening = !p.classList.contains('open');
    p.classList.toggle('open');
    if (opening) otaRefresh();
}

// Manual .bin upload (our own OTA — no ElegantOTA). Streams the file to
// /api/ota/upload with client-side progress, then reuses the verify overlay.
function otaManualUpload(input) {
    const f = input.files && input.files[0];
    input.value = '';                       // allow re-picking the same file
    if (!f) return;
    if (!confirm('Upload and flash “' + f.name + '”? The device will reboot.')) return;

    const st = $('otaStatus'), bar = $('otaBar'), fill = $('otaBarFill');
    const dl = $('otaDownload'); if (dl) dl.style.display = 'none';
    $('otaCheckBtn').disabled = true;
    $('otaBadge').style.display = 'none';
    $('otaNotesRow').style.display = 'none';
    st.className = 'ota-status'; bar.style.display = ''; fill.style.width = '0';
    _otaTarget = '';                         // unknown target for a manual bin
    _otaUpdating = true;

    const fd = new FormData(); fd.append('file', f, f.name);
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/ota/upload');
    xhr.upload.onprogress = e => {
        if (!e.lengthComputable) return;
        const p = Math.round(e.loaded * 100 / e.total);
        fill.style.width = p + '%';
        st.textContent = 'Uploading & writing… ' + p + '%';
    };
    xhr.onload = () => {
        if (xhr.status === 200) otaBeginVerify();       // device reboots → verify
        else {
            st.className = 'ota-status err';
            st.textContent = 'Upload failed (HTTP ' + xhr.status + ')';
            $('otaCheckBtn').disabled = false; _otaUpdating = false;
        }
    };
    // Connection dropping mid-flush usually means the device already rebooted.
    xhr.onerror = () => otaBeginVerify();
    xhr.send(fd);
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
    if (tbody) tbody.innerHTML = '<tr><td colspan="4" style="color:var(--txt4);text-align:center;padding:12px">Waiting for responses…</td></tr>';
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
        tbody.innerHTML = '<tr><td colspan="4" style="color:var(--txt4);text-align:center;padding:12px">No devices found.</td></tr>';
        return;
    }
    tbody.innerHTML = '';
    // Sort alphabetically by fixture ID (numeric-aware, case-insensitive).
    devices = devices.slice().sort((a, b) =>
        String(a.id || '').localeCompare(String(b.id || ''), undefined, {numeric: true, sensitivity: 'base'}));
    devices.forEach(d => {
        const row = document.createElement('tr');
        row.innerHTML =
            '<td><b>' + (d.id || '—') + '</b></td>' +
            '<td>' + (d.fixture || '—') + '</td>' +
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
    $('popupMdns').textContent    = d.mdns || ('rav' + (d.id || '') + '.local');
    $('popupRssi').textContent    = (d.mode === 'WiFi' || d.mode === 'AP-WiFi') ? formatRssi(d.rssi) : '—';
    $('popupFps').textContent     = (d.fps !== undefined ? d.fps : 0) + ' fps';
    $('popupMac').textContent     = d.mac || '—';
    $('popupFw').textContent      = d.fw || '—';
    $('popupTemp').textContent    = (d.temp > 0) ? d.temp.toFixed(1) + '°C' : '—';
    // d.uptime_sec is the new field (real seconds). Fall back to legacy
    // d.uptime (minutes ×60) for devices still on older firmware.
    const ups = (d.uptime_sec !== undefined) ? d.uptime_sec : (d.uptime || 0) * 60;
    $('popupUptime').textContent  = formatUptimeHHMM(ups);
    $('popupTotal').textContent   = formatTotalHours(d.total_hours);
    const hw = d.hwMac || '', ip = d.ip || '';
    $('popupActions').innerHTML =
        '<button type="button" class="pop-btn" onclick="window.open(\'http://' + ip + '/\',\'_blank\')">Open UI &rarr;</button>' +
        '<button type="button" class="pop-btn" onclick="sendDeviceCmd(\'' + ip + '\',\'HIGHLIGHT\',\'' + hw + '\')">Highlight</button>' +
        '<button type="button" class="pop-btn" onclick="sendDeviceCmd(\'' + ip + '\',\'CONNECT\',\'' + hw + '\')">Send WiFi</button>' +
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

// Live effects preview — every drag/click on the effects controls POSTs
// just the dmx.effects subobject so the LEDs respond without a Save click
// or restart. Debounced to ~120 ms so a slider drag doesn't drown the
// device in requests (only the most recent value within the window
// actually goes out). Picked over WebSocket because /api/config already
// applies effects live on the server side after the no-restart commit.
let _fxLiveTimer = null;
function liveSaveEffects() {
    if (_fxLiveTimer) clearTimeout(_fxLiveTimer);
    _fxLiveTimer = setTimeout(() => {
        const payload = { dmx: { effects: {
            effect:       parseInt(getVal('fxEffect'))      || 0,
            speed:        parseInt(getVal('fxSpeed'))       || 128,
            r:            parseInt(getVal('fxR'))           || 0,
            g:            parseInt(getVal('fxG'))           || 0,
            b:            parseInt(getVal('fxB'))           || 0,
            intensity:    parseInt(getVal('fxIntensity'))   || 255,
            rgbw:         getChk('fxRgbw') ? 1 : 0,
            white:        parseInt(getVal('fxWhite'))       || 0,
            strobe_rgb:   parseInt(getVal('fxStrobeRgb'))   || 0,
            strobe_white: parseInt(getVal('fxStrobeWhite')) || 0,
        }}};
        fetch('/api/config', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(payload),
        }).catch(() => { /* swallow; next slider event will retry */ });
    }, 120);
}
window.liveSaveEffects = liveSaveEffects;

// Expose init() to the serial loader in index.html. The loader fires init()
// AFTER all three scripts (app.js + output-card.js + fixture.js) are loaded
// — so by the time init() runs, window.renderFixture is already defined
// and the fixture-section render succeeds on first paint instead of
// flashing "No fixture renderer loaded.".
window.init = init;
