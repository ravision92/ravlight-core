// Orion fixture renderer — winch motor (TMC2209) + optional LED outputs.
// Save-relevant fields + action buttons + live status polling against
// /motorstatus. StallGuard calibration wizard and per-output LED cards are
// follow-up work.

(function () {
    const OC = window.outputCard;
    let _statusTimer = null;
    let _ledN = 0;
    let _F = {};
    let _unit = 'cm';  // 'cm' or 'in'

    // Fault-flag bitmask → human label. Must match the MotorFault enum in
    // include/core/motor/IMotorDriver.h: STALL=1<<0, OVERCURRENT=1<<1,
    // OVERTEMP=1<<2, DRIVER_ERROR=1<<3. The previous mapping was off by
    // chip-family default (TMC5160 layout) — when DIAG fired stall at the
    // end of a decelerated DMX move, the badge mislabeled it "overtemp".
    const FAULT_LABELS = [
        {bit: 0x01, label: 'stall'},
        {bit: 0x02, label: 'overcurrent'},
        {bit: 0x04, label: 'overtemp'},
        {bit: 0x08, label: 'driver-error'},
    ];

    function pollStatus() {
        fetch('/motorstatus').then(r => r.json()).then(s => {
            if (!s.available) { setStateBadge('NO DRIVER', '#a33'); return; }
            setStateBadge(s.state || '--', null);
            const posCm = Number(s.positionCm || 0);
            setText('orionPosCm', (_unit === 'in' ? (posCm / 2.54) : posCm).toFixed(2));
            const posUnitEl = document.getElementById('orionPosUnit');
            if (posUnitEl) posUnitEl.textContent = _unit;
            setText('orionSg',     s.sgResult);
            setText('orionTemp',   s.driverTemp);
            const homed = $('orionHomed');
            if (homed) {
                homed.textContent = s.homed ? 'homed' : 'not homed';
                homed.style.color = s.homed ? 'var(--acc)' : 'var(--txt3)';
            }
            // Travel display — converts cm → in when the user picked inches.
            if (s.upCm != null && s.downCm != null) {
                const travelCm = Math.abs(s.upCm - s.downCm);
                setText('orionTravelCm', (_unit === 'in' ? (travelCm / 2.54) : travelCm).toFixed(1));
            } else setText('orionTravelCm', '--');
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

    // ── Steps ⇄ physical-unit conversion ─────────────────────────────────────
    // stepsPerCm derives from the mechanical-calibration triplet:
    //   (stepsPerRev × gearRatio) / circumference_cm
    //   circumference_cm = π × drumDiameter_mm / 10
    // Recomputed on demand from the form fields so it tracks edits live.
    // Must match the server-side orionStepsPerCm() exactly, including the
    // fixed 1/16 microstepping multiplier configured in Tmc2209LocalDriver
    // init. Skipping the ×16 here was off by 16× — every cm value typed in
    // the form was saved 16× smaller and read back 16× smaller.
    function stepsPerCm() {
        const drumMm   = parseFloat(getV('oDrum'))     || 50;
        const gear     = parseFloat(getV('oGear'))     || 1;
        const stepsRev = parseFloat(getV('oStepsRev')) || 200;
        const circCm   = Math.PI * drumMm / 10;
        return circCm > 0 ? (stepsRev * 16 * gear) / circCm : 0;
    }
    function stepsToCm(steps) {
        const spc = stepsPerCm();
        return spc > 0 ? steps / spc : 0;
    }
    function cmToSteps(cm) {
        const spc = stepsPerCm();
        return Math.round(cm * spc);
    }
    // Convert an internal cm value to the unit currently displayed.
    function cmToDisplay(cm) { return (_unit === 'in') ? (cm / 2.54) : cm; }
    // Convert a value from the currently-displayed unit back to cm.
    function displayToCm(v)  { return (_unit === 'in') ? (Number(v) * 2.54) : Number(v); }
    function fmt(v) { return Number(v).toFixed(2); }
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

        // Mechanical params drive stepsPerCm — pre-load before computing display
        // values so the conversion is consistent with the user's actual rig.
        const drumDiameter  = num(fix.drumDiameterMm, 50);
        const stepsPerRev   = num(fix.motorStepsPerRev, 200);
        const gearRatio     = num(fix.gearRatio,     1);
        const _circCm = Math.PI * drumDiameter / 10;
        const _spc    = _circCm > 0 ? (stepsPerRev * 16 * gearRatio) / _circCm : 0;
        const stepsToDisp = (s) => {
            const cm = _spc > 0 ? s / _spc : 0;
            return (_unit === 'in') ? (cm / 2.54).toFixed(2) : cm.toFixed(2);
        };

        const downPositionDisp = stepsToDisp(num(fix.downPosition, 0));
        const upPositionDisp   = stepsToDisp(num(fix.upPosition,   10000));
        const maxSpeedDisp     = stepsToDisp(num(fix.maxSpeed,     6000));
        const maxAccelDisp     = stepsToDisp(num(fix.maxAccel,     8000));
        const jogSpeedDisp     = stepsToDisp(num(fix.jogSpeed,     2000));

        const homingDir     = num(fix.homingDirection, -1);
        const dmxWd         = num(fix.dmxWatchdogAction, 0);
        const sgthrs        = num(fix.operSgthrs,    100);

        let h = '';
        h += '<div class="acc-wrap"><div class="acc-body open"><div class="acc-inner">';

        // ── Status panel + action buttons ────────────────────────────────────
        h += '<div id="orionStatus" style="display:flex;flex-wrap:wrap;gap:10px;align-items:center;font-size:12px;color:var(--txt2)">';
        h += '  <span id="orionState" style="padding:4px 10px;border-radius:6px;background:#444;color:#fff;font-weight:600;font-size:11px;letter-spacing:.04em;text-transform:uppercase">--</span>';
        h += '  <span>pos: <b id="orionPosCm" style="color:var(--txt)">--</b> <span id="orionPosUnit">cm</span></span>';
        h += '  <span>sg: <b id="orionSg" style="color:var(--txt)">--</b></span>';
        h += '  <span>temp: <b id="orionTemp" style="color:var(--txt)">--</b>&deg;C</span>';
        h += '  <span id="orionHomed" style="color:var(--txt3)">not homed</span>';
        h += '  <span id="orionFaults" style="color:#c44"></span>';
        h += '</div>';

        h += '<div style="display:flex;gap:6px;margin-top:8px;flex-wrap:wrap">';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionPost(\'/home\')">Home</button>';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionSetHome()">Set as home</button>';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionPost(\'/clearfault\')">Clear Fault</button>';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px;background:var(--red);color:#fff;border-color:var(--red);font-weight:700;letter-spacing:.05em" onclick="orionPost(\'/estop\')">E-STOP</button>';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionPost(\'/highlight\')">Highlight</button>';
        h += '</div>';

        h += '<div style="height:6px"></div>';
        h += '<div class="ch-list">';

        // ── 1. DMX patch ─────────────────────────────────────────────────────
        h += cardOpen('DMX patch', 'oSumDmx');
        h += '<div class="field"><label class="lbl">Personality</label>';
        h += '  <select id="oPersonality" onchange="orionChMap()">';
        Object.keys(PERSONALITIES).forEach(k => h += opt(k, PERSONALITIES[k], Number(k) === personality));
        h += '  </select>';
        h += '</div>';
        h += '<div class="g2">';
        h += '  <div class="field"><label class="lbl">Position start (MSB)</label>';
        h += '    <input type="number" id="oPositionStart" min="1" max="511" value="' + positionStart + '" oninput="orionChMap()"></div>';
        h += '  <div class="field"><label class="lbl">Control start (Enable)</label>';
        h += '    <input type="number" id="oControlStart" min="1" max="510" value="' + controlStart + '" oninput="orionChMap()"></div>';
        h += '</div>';
        h += '<div class="div" style="margin:2px 0"></div>';
        h += '<span class="lbl">Channel map</span>';
        h += '<div id="orionChMap" style="margin-top:2px"></div>';
        h += '<p class="field-note">Position and Control are independent addresses inside the motor universe above.</p>';
        h += '<div class="div" style="margin:6px 0"></div>';
        h += '<div class="tog-row">';
        h += '  <input type="checkbox" id="oDmxInv"' + (_fix.dmxInvertPosition ? ' checked' : '') + '>';
        h += '  <span class="tog-lbl">Invert DMX direction (DMX 0 = home / retracted)</span>';
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
        h += '<button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionCalOpen()">Calibrate StallGuard (homing)</button>';
        h += '<div class="div" style="margin:6px 0"></div>';
        h += '<p class="field-note">Speed-aware profile (UP + DOWN sweep) for stall detection during jog and DMX moves.</p>';
        h += '<button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionSgProfileOpen()">Profile sweep wizard</button>';
        h += cardClose();

        // ── 3. Manual jog ────────────────────────────────────────────────────
        h += cardOpen('Manual jog (calibration)', 'oSumJog');
        h += '<div class="field"><label class="lbl">Jog speed (<span class="o-u">cm</span>/s)</label>';
        h += '  <input type="number" id="oJogSpeed" step="any" min="0.1" value="' + jogSpeedDisp + '"></div>';
        h += '<div style="display:flex;gap:6px;margin-top:6px">';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onmousedown="orionJog(1)" onmouseup="orionJogStop()" ontouchstart="orionJog(1)" ontouchend="orionJogStop()">&#x25B2; Jog Up</button>';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onmousedown="orionJog(-1)" onmouseup="orionJogStop()" ontouchstart="orionJog(-1)" ontouchend="orionJogStop()">&#x25BC; Jog Down</button>';
        h += '  <button type="button" id="oReleaseBtn" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionPost(\'/release-dmx\')" disabled>Release to DMX</button>';
        h += '</div>';
        h += '<p class="field-note">Hold to move. DMX commands are suspended after a jog until you click Release to DMX.</p>';
        h += '<div class="div" style="margin:6px 0"></div>';
        h += '<div class="tog-row">';
        h += '  <input type="checkbox" id="oJogOverride">';
        h += '  <span class="tog-lbl">Override soft limits (⚠ for setting travel positions only)</span>';
        h += '</div>';
        h += '<p class="field-note">When on, jog ignores the saved DOWN/UP limits — use to drive past them and then click "Set as DOWN/UP limit" to capture the new position.</p>';
        h += cardClose();

        // ── 4. Travel limits ─────────────────────────────────────────────────
        h += cardOpen('Travel limits', 'oSumTravel');
        h += '<div class="g2">';
        h += '  <div class="field"><label class="lbl">DOWN position (<span class="o-u">cm</span>)</label>';
        h += '    <input type="number" id="oDownPos" step="any" value="' + downPositionDisp + '"></div>';
        h += '  <div class="field"><label class="lbl">UP position (<span class="o-u">cm</span>)</label>';
        h += '    <input type="number" id="oUpPos" step="any" value="' + upPositionDisp + '"></div>';
        h += '</div>';
        h += '<div class="g2">';
        h += '  <div class="field"><label class="lbl">Max speed (<span class="o-u">cm</span>/s)</label>';
        h += '    <input type="number" id="oMaxSpeed" step="any" value="' + maxSpeedDisp + '"></div>';
        h += '  <div class="field"><label class="lbl">Max accel (<span class="o-u">cm</span>/s²)</label>';
        h += '    <input type="number" id="oMaxAccel" step="any" value="' + maxAccelDisp + '"></div>';
        h += '</div>';
        h += '<div style="display:flex;gap:6px;margin-top:6px">';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionSetLimit(\'down\')">Set as DOWN limit</button>';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionSetLimit(\'up\')">Set as UP limit</button>';
        h += '</div>';
        h += '<p class="field-note">Travel: <b id="orionTravelCm">--</b> <span class="o-u">cm</span></p>';
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
        h += '<div class="div" style="margin:6px 0"></div>';
        h += '<div class="tog-row">';
        h += '  <input type="checkbox" id="orionUnit"' + (_unit === 'in' ? ' checked' : '') + ' onchange="orionUnitChange()">';
        h += '  <span class="tog-lbl">Display in inches (off = cm)</span>';
        h += '</div>';
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

        document.getElementById('fixtureSection').innerHTML = h;

        // First-tab label
        const fh = document.getElementById('fixHeader');
        if (fh) fh.textContent = 'Motor';

        // ── LED outputs (Orion's secondary fixture role) → second tab ────────
        const fh2 = document.getElementById('fixHeader2');
        const sec2 = document.getElementById('fixtureSection2');
        if (_ledN > 0 && fh2 && sec2) {
            const leds = (fix.ledOutputs || []).slice();
            while (leds.length < _ledN) {
                leds.push({proto: 1, count: 0, univ: 0, ch: 1, group: 1, inv: 0, bri: 255, order: 'RGB'});
            }
            let h2 = '<div class="acc-wrap"><div class="acc-body open"><div class="acc-inner">';
            h2 += '  <span class="grp-lbl">LED outputs</span>';
            h2 += '  <div class="ch-list" id="orionLedCards"></div>';
            h2 += '  <div class="budget">';
            h2 += '    <span class="bud-lbl">Pixels</span>';
            h2 += '    <div class="bud-bar"><div class="bud-fill" id="orionBudFill"></div></div>';
            h2 += '    <span class="bud-val" id="orionBudVal">0 / 0</span>';
            h2 += '  </div>';
            h2 += '</div></div></div>';
            sec2.innerHTML = h2;

            const root = document.getElementById('orionLedCards');
            for (let i = 0; i < _ledN; i++) root.insertAdjacentHTML('beforeend', OC.render(leds[i], i, _F, _ledN));
            window.fixtureRecalc = orionLedRecalc;
            for (let i = 0; i < _ledN; i++) OC.protoChange(i);
            orionLedRecalc();

            fh2.textContent = 'LED';
            fh2.style.display = '';
        }

        // Apply unit display + initial channel-map render
        orionChMap();
        orionUnitChange();

        // Live polling — kick off after render. Clear any previous timer so a
        // re-render (e.g. after factory reset) doesn't spawn parallel pollers.
        // Skip ticks when the tab is hidden to avoid background traffic on the
        // ESP32, and keep the rate at 1.5 s — fast enough for live readouts
        // without piling onto the shared /api/status poll from app.js.
        if (_statusTimer) clearInterval(_statusTimer);
        pollStatus();
        _statusTimer = setInterval(() => {
            if (document.visibilityState === 'visible') pollStatus();
        }, 1500);
    };

    // ── Channel-map dynamic table ────────────────────────────────────────────
    // Rebuilds the per-channel list inside the DMX patch card based on the
    // currently selected personality + start addresses.
    window.orionChMap = function () {
        const el = document.getElementById('orionChMap');
        if (!el) return;
        const p  = parseInt(getV('oPersonality'))   || 1;
        const ps = parseInt(getV('oPositionStart')) || 1;
        const cs = parseInt(getV('oControlStart'))  || 3;
        const rows = [];
        if (p === 1) {
            rows.push([ps, 'Position (8-bit)']);
        } else {
            rows.push([ps, 'Position MSB']);
            rows.push([ps + 1, 'Position LSB']);
        }
        rows.push([cs,     'Enable']);
        rows.push([cs + 1, 'Speed']);
        if (p === 3) {
            rows.push([cs + 2, 'Acceleration']);
            rows.push([cs + 3, 'Function']);
        } else {
            rows.push([cs + 2, 'Function']);
        }
        rows.sort((a, b) => a[0] - b[0]);
        el.innerHTML = rows.map(r =>
            '<div style="display:flex;gap:10px;font-size:12px;padding:4px 0;border-bottom:1px solid var(--line)">' +
            '<span style="color:var(--acc);font-weight:600;min-width:38px">CH' + r[0] + '</span>' +
            '<span style="color:var(--txt2)">' + r[1] + '</span></div>').join('');
    };

    // ── Unit selector (cm ⇄ inch) ────────────────────────────────────────────
    // Stored values are raw stepper counts; the form shows them in the user's
    // chosen physical unit. Toggling the selector rescales every displayed
    // value (cm ⇄ in via 2.54) and rewrites all .o-u suffix spans in one pass.
    window.orionUnitChange = function () {
        const sel = document.getElementById('orionUnit');
        if (!sel) return;
        const prev = _unit;
        _unit = sel.checked ? 'in' : 'cm';
        if (prev !== _unit) {
            const factor = (prev === 'cm' && _unit === 'in') ? (1 / 2.54)
                         : (prev === 'in' && _unit === 'cm') ? 2.54
                         : 1;
            ['oJogSpeed', 'oMaxSpeed', 'oMaxAccel', 'oDownPos', 'oUpPos'].forEach(id => {
                const el = document.getElementById(id);
                if (!el || el.value === '') return;
                const v = parseFloat(el.value);
                if (!isNaN(v)) el.value = fmt(v * factor);
            });
        }
        document.querySelectorAll('.o-u').forEach(s => { s.textContent = _unit; });
        // Status-panel unit label updates immediately, even when TMC is offline
        // (in that case pollStatus early-returns and would never touch it).
        const posUnitEl = document.getElementById('orionPosUnit');
        if (posUnitEl) posUnitEl.textContent = _unit;
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
        // Jog speed input is in display units; backend wants raw steps/s.
        const speed = cmToSteps(displayToCm(getV('oJogSpeed'))) || 1000;
        const fd = new FormData();
        fd.append('dir',   dir > 0 ? 'up' : 'down');
        fd.append('speed', speed);
        const ovr = document.getElementById('oJogOverride');
        if (ovr && ovr.checked) fd.append('override', '1');
        fetch('/jog', {method: 'POST', body: fd}).then(r => {
            if (!r.ok) r.text().then(t => showToast('Jog: ' + (t || ('error ' + r.status))));
        }).catch(() => showToast('Jog: network error'));
    };
    window.orionJogStop = function () {
        fetch('/jogstop', {method: 'POST'}).catch(() => {});
    };

    // Manual override: declare home at the current position. Use when StallGuard
    // isn't an option (driver without current sensing) or when "home" is just a
    // chosen operating position rather than a mechanical end stop.
    window.orionSetHome = function () {
        if (!confirm('Set the current position as home? Soft limits and DMX position will use this as zero.')) return;
        fetch('/sethome', {method: 'POST'}).then(r => {
            if (r.ok) showToast('Home position set');
            else showToast('sethome failed');
        }).catch(() => showToast('sethome error'));
    };

    window.orionSetLimit = function (which) {
        const fd = new FormData();
        fd.append('which', which);
        fetch('/setlimit', {method: 'POST', body: fd}).then(r => {
            if (!r.ok) { showToast('setlimit failed'); return; }
            // Server returns the new limit in cm (one decimal). Push it into
            // the corresponding form input so the user sees what was saved,
            // converting to the currently selected display unit if needed.
            r.text().then(cm => {
                const id = which === 'down' ? 'oDownPos' : 'oUpPos';
                const el = document.getElementById(id);
                if (el) {
                    const cmNum = parseFloat(cm) || 0;
                    el.value = (_unit === 'in' ? (cmNum / 2.54) : cmNum).toFixed(2);
                }
                showToast(which.toUpperCase() + ' limit = ' + cm + ' cm');
            });
        }).catch(() => showToast('setlimit error'));
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
            // Sample SG every 100 ms while held. The avg is computed only on
            // "stable" samples (after the acceleration ramp + PWM autoscale
            // settle window — see CAL_SETTLE_MS below). Pre-settle samples are
            // still pushed but tagged with a flag so the trim is consistent.
            _cal.t0 = Date.now();
            _cal.timer = setInterval(() => {
                fetch('/motorstatus').then(r => r.json()).then(s => {
                    if (s.available && s.sgResult != null) {
                        const dt = Date.now() - _cal.t0;
                        _cal.samples.push({v: s.sgResult, t: dt});
                        setText('oCalSgLive', s.sgResult);
                        const stable = _cal.samples.filter(x => x.t >= 800);
                        setText('oCalN', stable.length + '/' + _cal.samples.length);
                        if (stable.length) {
                            const avg = stable.reduce((a, b) => a + b.v, 0) / stable.length;
                            setText('oCalAvg', avg.toFixed(1));
                        } else {
                            setText('oCalAvg', '(settling…)');
                        }
                    }
                }).catch(() => {});
            }, 100);
        });
    };

    window.orionCalStopMeasure = function () {
        if (!_cal) return;
        if (_cal.timer) { clearInterval(_cal.timer); _cal.timer = null; }
        fetch('/sgcalstop', {method: 'POST'}).catch(() => {});

        // Drop the first 800 ms of samples (accel ramp + PWM autoscale settle).
        // Also drop the last ~200 ms in case the user releases mid-motion and
        // the motor's already decelerating before the timer is cleared.
        const stable = _cal.samples
            .filter(x => x.t >= 800)
            .slice(0, Math.max(1, _cal.samples.filter(x => x.t >= 800).length - 2));
        if (stable.length < 5) {
            setText('oCalMsg', 'Need a longer hold (≥ 1.5 s) to get a stable reading.');
            return;
        }
        const avg = stable.reduce((a, b) => a + b.v, 0) / stable.length;
        if (_cal.step === 0) {
            _cal.free = avg;
            _cal.step = 1;
            setText('oCalMsg', 'Step 1 done — free run SG ≈ ' + avg.toFixed(1) + '. Now load the rig with the real fixture and hold to measure again.');
            $('oCalRun').textContent = 'Hold to measure (loaded)';
        } else {
            _cal.loaded = avg;
            // TMC2209: DIAG asserts when SG_RESULT ≤ 2*SGTHRS. To trigger only
            // under real load (not at free-run SG), we want 2*SGTHRS at the
            // midpoint of (loaded, free) — i.e. SGTHRS = (free + loaded) / 4.
            let sgthrs = Math.round((_cal.free + _cal.loaded) / 4);
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

    // ── Adaptive SG profile sweep wizard ─────────────────────────────────────
    // Two-step modal: sweep UP direction, then DOWN. Each sweep runs the motor
    // for ~14 s (8 bins × 1.5 s) while the device captures SG_RESULT vs speed
    // statistics. The resulting profile is auto-persisted to NVS at the end.

    let _swp = null;  // {step: 0|1, timer, baseSpeed, speedStep}

    window.orionSgProfileOpen = function () {
        let m = $('orionSwpModal');
        if (!m) {
            m = document.createElement('div');
            m.id = 'orionSwpModal';
            m.className = 'modal-overlay';
            document.body.appendChild(m);
        }
        const maxSpd = parseFloat(getV('oMaxSpeed')) || 10;
        const maxSpdSteps = cmToSteps(displayToCm(maxSpd));
        const step = Math.max(500, Math.round(maxSpdSteps / 8));
        const base = Math.max(3000, step);
        // The wizard is typically launched right after homing, so the motor
        // sits at the homing end. Start the sweep in the OPPOSITE direction
        // (the side that still has full travel available), then do the other.
        const homDir = parseInt(getV('oHomingDir')) || -1;
        const first  = (homDir < 0) ? 'up'   : 'down';
        const second = (homDir < 0) ? 'down' : 'up';
        m.innerHTML =
            '<div class="modal-box" style="text-align:left;max-width:420px">' +
            '  <h3>Adaptive SG profile sweep</h3>' +
            '  <p class="field-note">Builds a speed-and-direction baseline of SG_RESULT under no load. Once saved, real stalls during jog/DMX trip a fault automatically.</p>' +
            '  <p class="field-note">⚠ Make sure the rope is free to travel — the motor will run ' + first.toUpperCase() + ' first (opposite of homing), then ' + second.toUpperCase() + ', ~14 s each direction.</p>' +
            '  <div class="field-note" id="oSwpStatus">Ready. Press Start to begin with ' + first.toUpperCase() + '.</div>' +
            '  <div class="bud-bar" style="margin:10px 0"><div class="bud-fill" id="oSwpFill" style="background:#e9ff00"></div></div>' +
            '  <p class="field-note">Direction: <b id="oSwpDir">--</b> &middot; bin: <b id="oSwpBin">0</b>/8 &middot; SG: <b id="oSwpSg">--</b></p>' +
            '  <div class="modal-btns" style="flex-direction:column;gap:8px">' +
            '    <button type="button" class="act-btn" id="oSwpStartA" style="width:100%" onclick="orionSgSweepRun(\''+ first +'\','+ base +','+ step +',\''+ second +'\')">Start ' + first.toUpperCase() + ' sweep</button>' +
            '    <button type="button" class="act-btn" id="oSwpStartB" style="width:100%;display:none">Start second sweep</button>' +
            '    <button type="button" class="modal-cancel" onclick="orionSgProfileClose()">Close</button>' +
            '  </div>' +
            '</div>';
        m.classList.add('open');
        _swp = {step: 0, baseSpeed: base, speedStep: step, first: first, second: second};
    };

    window.orionSgProfileClose = function () {
        if (_swp && _swp.timer) clearInterval(_swp.timer);
        fetch('/sgsweepstop', {method: 'POST'}).catch(() => {});
        _swp = null;
        const m = $('orionSwpModal');
        if (m) m.classList.remove('open');
    };

    window.orionSgSweepRun = function (dir, base, step, nextDir) {
        if (!_swp) return;
        setText('oSwpDir', dir.toUpperCase());
        $('oSwpStartA').disabled = true;
        $('oSwpStartB').disabled = true;
        setText('oSwpStatus', 'Running ' + dir.toUpperCase() + ' sweep…');
        const fd = new FormData();
        fd.append('dir',  dir);
        fd.append('base', base);
        fd.append('step', step);
        fetch('/sgsweep', {method:'POST', body:fd}).then(r => {
            if (!r.ok) {
                r.text().then(t => setText('oSwpStatus', 'rejected: ' + t));
                $('oSwpStartA').disabled = false;
                return;
            }
            _swp.timer = setInterval(async () => {
                const p = await (await fetch('/sgsweep')).json();
                setText('oSwpBin', p.currentBin);
                setText('oSwpSg',  p.lastSg);
                const pct = (p.currentBin / p.totalBins) * 100;
                const fill = $('oSwpFill');
                if (fill) fill.style.width = pct + '%';
                if (p.phase === 2) {
                    clearInterval(_swp.timer); _swp.timer = null;
                    if (nextDir) {
                        setText('oSwpStatus', dir.toUpperCase() + ' done. Now run ' + nextDir.toUpperCase() + '.');
                        $('oSwpStartA').style.display = 'none';
                        const b2 = $('oSwpStartB');
                        b2.style.display = '';
                        b2.disabled      = false;
                        b2.textContent   = 'Start ' + nextDir.toUpperCase() + ' sweep';
                        b2.onclick = function () {
                            orionSgSweepRun(nextDir, _swp.baseSpeed, _swp.speedStep, null);
                        };
                    } else {
                        setText('oSwpStatus', '✓ Profile saved. Stall detection now active in jog/DMX moves.');
                        $('oSwpStartB').style.display = 'none';
                    }
                } else if (p.phase === 3) {
                    clearInterval(_swp.timer); _swp.timer = null;
                    setText('oSwpStatus', 'Sweep aborted.');
                    $('oSwpStartA').disabled = false;
                }
            }, 250);
        });
    };

    // ── Serialise → /api/config ──────────────────────────────────────────────

    window.getFixtureData = function (features) {
        // Displayed values are in _unit (cm or in); convert back to raw steps.
        const toSteps = (id) => cmToSteps(displayToCm(getV(id)));
        const out = {
            personality:       parseInt(getV('oPersonality'))   || 1,
            positionStart:     parseInt(getV('oPositionStart')) || 1,
            controlStart:      parseInt(getV('oControlStart'))  || 3,
            downPosition:      toSteps('oDownPos'),
            upPosition:        toSteps('oUpPos')   || 10000,
            maxSpeed:          toSteps('oMaxSpeed') || 6000,
            maxAccel:          toSteps('oMaxAccel') || 8000,
            jogSpeed:          toSteps('oJogSpeed') || 2000,
            drumDiameterMm:    parseInt(getV('oDrum'))          || 50,
            motorStepsPerRev:  parseInt(getV('oStepsRev'))      || 200,
            gearRatio:         parseFloat(getV('oGear'))        || 1,
            homingDirection:   parseInt(getV('oHomingDir'))     || -1,
            dmxInvertPosition: !!(document.getElementById('oDmxInv') && document.getElementById('oDmxInv').checked),
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
