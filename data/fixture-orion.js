// Orion fixture renderer — winch motor (TMC2209) + optional LED outputs.
// Save-relevant fields + action buttons + live status polling against
// /motorstatus. StallGuard calibration wizard and per-output LED cards are
// follow-up work.

(function () {
    const OC = window.outputCard;
    let _statusTimer = null;
    let _ledN = 0;
    let _F = {};

    // Fault-flag bitmask → human label (mirrors enum in include/core/motor/IMotorDriver.h)
    const FAULT_LABELS = [
        {bit: 0x01, label: 'overtemp'},
        {bit: 0x02, label: 'short'},
        {bit: 0x04, label: 'open-load'},
        {bit: 0x08, label: 'stall'},
        {bit: 0x10, label: 'comm-lost'},
    ];

    function pollStatus() {
        fetch('/motorstatus').then(r => r.json()).then(s => {
            if (!s.available) { setStateBadge('NO DRIVER', '#a33'); return; }
            setStateBadge(s.state || '--', null);
            setText('orionPosCm',  Number(s.positionCm || 0).toFixed(2));
            setText('orionSg',     s.sgResult);
            setText('orionTemp',   s.driverTemp);
            const homed = $('orionHomed');
            if (homed) {
                homed.textContent = s.homed ? 'homed' : 'not homed';
                homed.style.color = s.homed ? 'var(--acc)' : 'var(--txt3)';
            }
            // Travel display
            setText('orionTravelCm', (s.upCm != null && s.downCm != null) ? Math.abs(s.upCm - s.downCm).toFixed(1) : '--');
            // Fault flags
            const fbox = $('orionFaults');
            if (fbox) {
                const ff = s.faultFlags || 0;
                if (ff) {
                    fbox.textContent = 'FAULT: ' + FAULT_LABELS.filter(f => ff & f.bit).map(f => f.label).join(', ');
                } else fbox.textContent = '';
            }
            // SGTHRS live readout
            setText('oSgthVal', s.operSgthrs);
            // Jog release button enable state
            const rel = $('oReleaseBtn');
            if (rel) rel.disabled = !s.override;
        }).catch(() => { /* transient */ });
    }

    function setStateBadge(text, color) {
        const el = $('orionState');
        if (!el) return;
        el.textContent = text;
        // Colour heuristics — green when running normally, red on fault/estop
        const c = color || (
            text === 'IDLE'    ? '#2a8' :
            text === 'MOVING'  ? '#2a8' :
            text === 'HOMING'  ? '#a82' :
            text === 'FAULT'   ? '#a33' :
            text === 'ESTOP'   ? '#a33' :
            text === 'BACKOFF' ? '#a82' : '#444'
        );
        el.style.background = c;
    }

    function $(id)              { return document.getElementById(id); }
    function setText(id, v)     { const el = $(id); if (el) el.textContent = (v === undefined || v === null) ? '--' : v; }
    const PERSONALITIES = {
        1: '1 — Basic (4 ch, 8-bit pos)',
        2: '2 — Basic HD (5 ch, 16-bit pos)',
        3: '3 — Standard (6 ch, +accel)',
    };
    const WD_ACTIONS = {
        0: 'E-stop',
        1: 'Return to home (slow)',
    };

    // Reusable accordion card open/close helpers — match the legacy ORION_HTML
    // collapsible sub-cards (.ch-card / .ch-head / .ch-body).
    function cardOpen(title, sumId) {
        let h = '<div class="ch-card">';
        h += '  <div class="ch-head" style="grid-template-columns:1fr 24px" onclick="orionToggleCard(this)">';
        h += '    <div class="ch-sum">';
        h += '      <span class="ch-proto">' + title + '</span>';
        h += '      <div class="ch-tags"><span class="ch-tag" id="' + sumId + '"></span></div>';
        h += '    </div>';
        h += '    <span class="ch-arr">&#9661;</span>';
        h += '  </div>';
        h += '  <div class="ch-body"><div class="ch-form">';
        return h;
    }
    function cardClose() { return '</div></div></div>'; }

    function num(v, dflt) { return (v !== undefined && v !== null) ? Number(v) : dflt; }
    function opt(value, label, selected) {
        return '<option value="' + value + '"' + (selected ? ' selected' : '') + '>' + label + '</option>';
    }

    let _fix = {};

    window.renderFixture = function (fix, features) {
        _fix = fix || {};
        _F   = features || {};
        _ledN = _F.hw_outputs || 0;
        const personality   = num(fix.personality,   1);
        const positionStart = num(fix.positionStart, 1);
        const controlStart  = num(fix.controlStart,  3);
        const downPosition  = num(fix.downPosition,  0);
        const upPosition    = num(fix.upPosition,    10000);
        const maxSpeed      = num(fix.maxSpeed,      6000);
        const maxAccel      = num(fix.maxAccel,      8000);
        const jogSpeed      = num(fix.jogSpeed,      2000);
        const drumDiameter  = num(fix.drumDiameterMm, 50);
        const stepsPerRev   = num(fix.motorStepsPerRev, 200);
        const gearRatio     = num(fix.gearRatio,     1);
        const homingDir     = num(fix.homingDirection, -1);
        const dmxWd         = num(fix.dmxWatchdogAction, 0);
        const sgthrs        = num(fix.operSgthrs,    100);

        let h = '';
        h += '<div class="acc-wrap"><div class="acc-body open"><div class="acc-inner">';

        // ── Status panel + action buttons ────────────────────────────────────
        h += '<div id="orionStatus" style="display:flex;flex-wrap:wrap;gap:10px;align-items:center;font-size:12px;color:var(--txt2)">';
        h += '  <span id="orionState" style="padding:4px 10px;border-radius:6px;background:#444;color:#fff;font-weight:600;font-size:11px;letter-spacing:.04em;text-transform:uppercase">--</span>';
        h += '  <span>pos: <b id="orionPosCm" style="color:var(--txt)">--</b> cm</span>';
        h += '  <span>sg: <b id="orionSg" style="color:var(--txt)">--</b></span>';
        h += '  <span>temp: <b id="orionTemp" style="color:var(--txt)">--</b>&deg;C</span>';
        h += '  <span id="orionHomed" style="color:var(--txt3)">not homed</span>';
        h += '  <span id="orionFaults" style="color:#c44"></span>';
        h += '</div>';

        h += '<div style="display:flex;gap:6px;margin-top:8px;flex-wrap:wrap">';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionPost(\'/home\')">Home</button>';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionPost(\'/clearfault\')">Clear Fault</button>';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px;color:var(--red);border-color:rgba(255,85,51,.35)" onclick="orionPost(\'/estop\')">E-stop</button>';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionPost(\'/highlight\')">Highlight</button>';
        h += '</div>';

        h += '<div style="height:8px"></div>';
        h += '<div class="ch-list">';

        // ── 1. DMX patch ─────────────────────────────────────────────────────
        h += cardOpen('DMX patch', 'oSumDmx');
        h += '<div class="field"><label class="lbl">Personality</label>';
        h += '  <select id="oPersonality">';
        Object.keys(PERSONALITIES).forEach(k => h += opt(k, PERSONALITIES[k], Number(k) === personality));
        h += '  </select>';
        h += '</div>';
        h += '<div class="g2">';
        h += '  <div class="field"><label class="lbl">Position start (MSB)</label>';
        h += '    <input type="number" id="oPositionStart" min="1" max="511" value="' + positionStart + '"></div>';
        h += '  <div class="field"><label class="lbl">Control start (Enable)</label>';
        h += '    <input type="number" id="oControlStart" min="1" max="510" value="' + controlStart + '"></div>';
        h += '</div>';
        h += cardClose();

        // ── 2. Homing ────────────────────────────────────────────────────────
        h += cardOpen('Homing', 'oSumHome');
        h += '<div class="field"><label class="lbl">Direction</label>';
        h += '  <select id="oHomingDir">';
        h += opt('-1', 'Down (-)', homingDir === -1);
        h += opt('1',  'Up (+)',   homingDir === 1);
        h += '  </select>';
        h += '</div>';
        h += '<p class="field-note">Homing detects the end stop via StallGuard. The operating threshold is tuned with the calibration wizard against the real hung load.</p>';
        h += '<div class="field"><label class="lbl">StallGuard threshold (SGTHRS)</label>';
        h += '  <input type="number" id="oSgthrs" min="0" max="255" value="' + sgthrs + '"></div>';
        h += '<p class="field-note">Live readout: SGTHRS = <b id="oSgthVal">--</b></p>';
        h += '<button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionCalOpen()">Calibrate StallGuard</button>';
        h += cardClose();

        // ── 3. Manual jog ────────────────────────────────────────────────────
        h += cardOpen('Manual jog (calibration)', 'oSumJog');
        h += '<div class="field"><label class="lbl">Jog speed (steps/s)</label>';
        h += '  <input type="number" id="oJogSpeed" min="1" value="' + jogSpeed + '"></div>';
        h += '<div style="display:flex;gap:6px;margin-top:6px">';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onmousedown="orionJog(1)" onmouseup="orionJogStop()" ontouchstart="orionJog(1)" ontouchend="orionJogStop()">&#x25B2; Jog Up</button>';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onmousedown="orionJog(-1)" onmouseup="orionJogStop()" ontouchstart="orionJog(-1)" ontouchend="orionJogStop()">&#x25BC; Jog Down</button>';
        h += '  <button type="button" id="oReleaseBtn" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionPost(\'/release-dmx\')" disabled>Release to DMX</button>';
        h += '</div>';
        h += '<p class="field-note">Hold to move. DMX commands are suspended after a jog until you click Release to DMX.</p>';
        h += cardClose();

        // ── 4. Travel limits ─────────────────────────────────────────────────
        h += cardOpen('Travel limits', 'oSumTravel');
        h += '<div class="g2">';
        h += '  <div class="field"><label class="lbl">DOWN position (steps)</label>';
        h += '    <input type="number" id="oDownPos" value="' + downPosition + '"></div>';
        h += '  <div class="field"><label class="lbl">UP position (steps)</label>';
        h += '    <input type="number" id="oUpPos" value="' + upPosition + '"></div>';
        h += '</div>';
        h += '<div class="g2">';
        h += '  <div class="field"><label class="lbl">Max speed (steps/s)</label>';
        h += '    <input type="number" id="oMaxSpeed" value="' + maxSpeed + '"></div>';
        h += '  <div class="field"><label class="lbl">Max accel (steps/s²)</label>';
        h += '    <input type="number" id="oMaxAccel" value="' + maxAccel + '"></div>';
        h += '</div>';
        h += '<div style="display:flex;gap:6px;margin-top:6px">';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionSetLimit(\'down\')">Set as DOWN limit</button>';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionSetLimit(\'up\')">Set as UP limit</button>';
        h += '</div>';
        h += '<p class="field-note">Travel: <b id="orionTravelCm">--</b> cm</p>';
        h += cardClose();

        // ── 5. Mechanical calibration ────────────────────────────────────────
        h += cardOpen('Mechanical calibration', 'oSumMech');
        h += '<div class="g2">';
        h += '  <div class="field"><label class="lbl">Drum diameter (mm)</label>';
        h += '    <input type="number" id="oDrum" min="1" value="' + drumDiameter + '"></div>';
        h += '  <div class="field"><label class="lbl">Steps per revolution</label>';
        h += '    <input type="number" id="oStepsRev" min="1" value="' + stepsPerRev + '"></div>';
        h += '</div>';
        h += '<div class="field"><label class="lbl">Gear ratio</label>';
        h += '  <input type="number" id="oGear" min="1" step="0.01" value="' + gearRatio + '"></div>';
        h += '<p class="field-note">Used to convert cm targets into stepper steps. Only matters if you key in cm-based limits; the saved limits above are raw steps.</p>';
        h += cardClose();

        // ── 6. Safety ────────────────────────────────────────────────────────
        h += cardOpen('Safety', 'oSumSafety');
        h += '<div class="field"><label class="lbl">On DMX loss</label>';
        h += '  <select id="oWdAction">';
        Object.keys(WD_ACTIONS).forEach(k => h += opt(k, WD_ACTIONS[k], Number(k) === dmxWd));
        h += '  </select>';
        h += '</div>';
        h += cardClose();

        h += '</div>';  // /.ch-list (motor sub-cards)
        h += '</div></div></div>';

        // ── LED outputs (Orion's secondary fixture role) ─────────────────────
        if (_ledN > 0) {
            const leds = (fix.ledOutputs || []).slice();
            while (leds.length < _ledN) {
                leds.push({proto: 1, count: 0, univ: 0, ch: 1, group: 1, inv: 0, bri: 255, order: 'RGB'});
            }
            h += '<div class="acc-wrap" style="margin-top:10px"><div class="acc-body open"><div class="acc-inner">';
            h += '  <span class="grp-lbl">LED outputs</span>';
            h += '  <div class="ch-list" id="orionLedCards"></div>';
            h += '  <div class="budget">';
            h += '    <span class="bud-lbl">Pixels</span>';
            h += '    <div class="bud-bar"><div class="bud-fill" id="orionBudFill"></div></div>';
            h += '    <span class="bud-val" id="orionBudVal">0 / 0</span>';
            h += '  </div>';
            h += '</div></div></div>';
        }

        document.getElementById('fixtureSection').innerHTML = h;

        if (_ledN > 0) {
            const leds = (fix.ledOutputs || []).slice();
            while (leds.length < _ledN) {
                leds.push({proto: 1, count: 0, univ: 0, ch: 1, group: 1, inv: 0, bri: 255, order: 'RGB'});
            }
            const root = document.getElementById('orionLedCards');
            for (let i = 0; i < _ledN; i++) root.insertAdjacentHTML('beforeend', OC.render(leds[i], i, _F, _ledN));
            window.fixtureRecalc = orionLedRecalc;
            for (let i = 0; i < _ledN; i++) OC.protoChange(i);
            orionLedRecalc();
        }

        // Live polling — kick off after render. Clear any previous timer so a
        // re-render (e.g. after factory reset) doesn't spawn parallel pollers.
        if (_statusTimer) clearInterval(_statusTimer);
        pollStatus();
        _statusTimer = setInterval(pollStatus, 1000);
    };

    function orionLedRecalc() {
        let total = 0;
        for (let i = 0; i < _ledN; i++) {
            const cnt = parseInt((document.getElementById('count' + i) || {value: '0'}).value) || 0;
            total += cnt;
            OC.sumUpdate(i);
        }
        const cap = _F.i2s ? 8192 : 4096;
        const pct = Math.min(100, (total / cap) * 100);
        const fill = document.getElementById('orionBudFill');
        if (fill) { fill.style.width = pct + '%'; fill.style.background = pct > 90 ? '#ff5533' : pct > 70 ? '#ff9900' : '#e9ff00'; }
        const bv = document.getElementById('orionBudVal');
        if (bv) bv.textContent = total + ' / ' + cap + (total > cap ? ' ⚠' : '');
    }

    window.orionToggleCard = function (head) {
        const card = head.parentElement;
        const body = card.querySelector('.ch-body');
        if (!body) return;
        const opening = !card.classList.contains('open');
        card.classList.toggle('open');
        if (opening) {
            body.style.maxHeight = body.scrollHeight + 'px';
            body.addEventListener('transitionend', function h() {
                body.style.maxHeight = 'none';
                body.removeEventListener('transitionend', h);
            }, {once: true});
        } else {
            body.style.maxHeight = body.scrollHeight + 'px';
            requestAnimationFrame(() => requestAnimationFrame(() => {
                body.style.maxHeight = '0';
            }));
        }
    };

    // ── Action endpoints ─────────────────────────────────────────────────────

    window.orionPost = function (path) {
        fetch(path, {method: 'POST'}).then(r => {
            if (!r.ok) showToast(path + ' failed');
        }).catch(() => showToast(path + ' error'));
    };

    let _jogInterval = null;
    window.orionJog = function (dir) {
        const speed = parseInt(document.getElementById('oJogSpeed').value) || 1000;
        const fd = new FormData();
        fd.append('dir',   dir);
        fd.append('speed', speed);
        fetch('/jog', {method: 'POST', body: fd}).catch(() => {});
    };
    window.orionJogStop = function () {
        fetch('/jogstop', {method: 'POST'}).catch(() => {});
    };

    window.orionSetLimit = function (which) {
        const fd = new FormData();
        fd.append('which', which);
        fetch('/setlimit', {method: 'POST', body: fd}).catch(() => showToast('setlimit failed'));
    };

    // ── StallGuard calibration wizard ────────────────────────────────────────
    // A 3-step modal. While the motor is running we sample sgResult from
    // /motorstatus and average; midpoint of (free, loaded) becomes the new
    // SGTHRS. Server endpoints used: /sgcal?dir= , /sgcalstop , /sgthreshold

    let _cal = null;  // {dir, step, samples, timer, free, loaded}

    window.orionCalOpen = function () {
        // Build a transient modal — appended to body, removed on close.
        let m = $('orionCalModal');
        if (!m) {
            m = document.createElement('div');
            m.id = 'orionCalModal';
            m.className = 'modal-overlay';
            document.body.appendChild(m);
        }
        const dir = parseInt(getV('oHomingDir')) || -1;
        m.innerHTML =
            '<div class="modal-box" style="text-align:left;max-width:380px">' +
            '  <h3>StallGuard calibration</h3>' +
            '  <p id="oCalMsg" class="field-note">Pick a direction and run the motor unloaded, then loaded. The midpoint becomes your operating SGTHRS.</p>' +
            '  <div class="field"><label class="lbl">Direction</label>' +
            '    <select id="oCalDir">' +
            '      <option value="-1"' + (dir === -1 ? ' selected' : '') + '>Down (-)</option>' +
            '      <option value="1"'  + (dir === 1  ? ' selected' : '') + '>Up (+)</option>' +
            '    </select>' +
            '  </div>' +
            '  <p id="oCalLive" class="field-note">SG live: <b id="oCalSgLive">--</b> &middot; samples: <b id="oCalN">0</b> &middot; avg: <b id="oCalAvg">--</b></p>' +
            '  <div class="field-note" id="oCalResult"></div>' +
            '  <div class="modal-btns" style="flex-direction:column;gap:8px">' +
            '    <button type="button" class="act-btn" id="oCalRun"   style="width:100%" onmousedown="orionCalStartMeasure()" onmouseup="orionCalStopMeasure()" ontouchstart="orionCalStartMeasure()" ontouchend="orionCalStopMeasure()">Hold to measure (no load)</button>' +
            '    <button type="button" class="act-btn" id="oCalSave"  style="width:100%;display:none" onclick="orionCalSave()">Save SGTHRS</button>' +
            '    <button type="button" class="modal-cancel" onclick="orionCalClose()">Close</button>' +
            '  </div>' +
            '</div>';
        m.classList.add('open');
        _cal = {dir: dir, step: 0, samples: [], timer: null, free: null, loaded: null};
    };

    window.orionCalClose = function () {
        if (_cal && _cal.timer) clearInterval(_cal.timer);
        _cal = null;
        const m = $('orionCalModal');
        if (m) m.classList.remove('open');
        // Make sure no cal is still running on the device.
        fetch('/sgcalstop', {method: 'POST'}).catch(() => {});
    };

    window.orionCalStartMeasure = function () {
        if (!_cal) return;
        _cal.samples = [];
        _cal.dir = parseInt(getV('oCalDir')) || -1;
        // Start the server-side cal run (motor moves at homing speed).
        const fd = new FormData();
        fd.append('dir', _cal.dir > 0 ? 'up' : 'down');
        fetch('/sgcal', {method: 'POST', body: fd}).then(r => {
            if (!r.ok) {
                r.text().then(t => setText('oCalMsg', 'sg cal rejected: ' + t));
                return;
            }
            // Sample SG values from /motorstatus every 100 ms while held.
            _cal.timer = setInterval(() => {
                fetch('/motorstatus').then(r => r.json()).then(s => {
                    if (s.available && s.sgResult != null) {
                        _cal.samples.push(s.sgResult);
                        setText('oCalSgLive', s.sgResult);
                        setText('oCalN', _cal.samples.length);
                        const avg = _cal.samples.reduce((a, b) => a + b, 0) / _cal.samples.length;
                        setText('oCalAvg', avg.toFixed(1));
                    }
                }).catch(() => {});
            }, 100);
        });
    };

    window.orionCalStopMeasure = function () {
        if (!_cal) return;
        if (_cal.timer) { clearInterval(_cal.timer); _cal.timer = null; }
        fetch('/sgcalstop', {method: 'POST'}).catch(() => {});

        if (_cal.samples.length < 5) {
            setText('oCalMsg', 'Need a longer hold to get a stable reading.');
            return;
        }
        const avg = _cal.samples.reduce((a, b) => a + b, 0) / _cal.samples.length;
        if (_cal.step === 0) {
            _cal.free = avg;
            _cal.step = 1;
            setText('oCalMsg', 'Step 1 done — free run SG ≈ ' + avg.toFixed(1) + '. Now load the rig with the real fixture and hold to measure again.');
            $('oCalRun').textContent = 'Hold to measure (loaded)';
        } else {
            _cal.loaded = avg;
            // Midpoint of free and loaded — same rule the legacy wizard used.
            let sgthrs = Math.round((_cal.free + _cal.loaded) / 2);
            if (sgthrs < 1)   sgthrs = 1;
            if (sgthrs > 255) sgthrs = 255;
            _cal.result = sgthrs;
            const warn = (_cal.free <= _cal.loaded) ?
                ' ⚠ free SG is not higher than loaded — verify hung load and direction.' : '';
            setText('oCalResult', 'Free: ' + _cal.free.toFixed(1) + ' · Loaded: ' + _cal.loaded.toFixed(1) +
                ' → SGTHRS = ' + sgthrs + warn);
            $('oCalRun').style.display  = 'none';
            $('oCalSave').style.display = '';
        }
    };

    window.orionCalSave = function () {
        if (!_cal || _cal.result == null) return;
        const fd = new FormData();
        fd.append('value', _cal.result);
        fetch('/sgthreshold', {method: 'POST', body: fd}).then(r => {
            if (r.ok) {
                showToast('SGTHRS = ' + _cal.result + ' saved');
                const inp = $('oSgthrs');
                if (inp) inp.value = _cal.result;
                orionCalClose();
            } else { showToast('save failed'); }
        });
    };

    // ── Serialise → /api/config ──────────────────────────────────────────────

    window.getFixtureData = function (features) {
        const out = {
            personality:       parseInt(getV('oPersonality'))   || 1,
            positionStart:     parseInt(getV('oPositionStart')) || 1,
            controlStart:      parseInt(getV('oControlStart'))  || 3,
            downPosition:      parseInt(getV('oDownPos'))       || 0,
            upPosition:        parseInt(getV('oUpPos'))         || 10000,
            maxSpeed:          parseInt(getV('oMaxSpeed'))      || 6000,
            maxAccel:          parseInt(getV('oMaxAccel'))      || 8000,
            jogSpeed:          parseInt(getV('oJogSpeed'))      || 2000,
            drumDiameterMm:    parseInt(getV('oDrum'))          || 50,
            motorStepsPerRev:  parseInt(getV('oStepsRev'))      || 200,
            gearRatio:         parseFloat(getV('oGear'))        || 1,
            homingDirection:   parseInt(getV('oHomingDir'))     || -1,
            dmxWatchdogAction: parseInt(getV('oWdAction'))      || 0,
            operSgthrs:        parseInt(getV('oSgthrs'))        || 100,
        };
        if (_ledN > 0) {
            const leds = [];
            for (let i = 0; i < _ledN; i++) leds.push(OC.read(i, features || _F));
            out.ledOutputs = leds;
        }
        return out;
    };

    function getV(id) { const el = document.getElementById(id); return el ? el.value : ''; }
})();
