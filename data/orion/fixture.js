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
            const homed = $('orionHomed');
            if (homed) {
                homed.textContent = s.homed ? 'homed' : 'not homed';
                homed.style.color = s.homed ? 'var(--acc)' : 'var(--txt3)';
            }
            updateHomingHint(s.homed);
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
        // Backend state strings are lowercase — compare case-insensitively.
        // Palette tuned for legibility against the dark UI: idle/jogging =
        // green, moving/backoff = orange, homing = blue, fault/estop = red,
        // driver_off = gray.
        const k = (text || '').toLowerCase();
        const c = color || (
            k === 'idle'       ? '#2e9c4a' :
            k === 'jogging'    ? '#2e9c4a' :
            k === 'moving'     ? '#e89a1b' :
            k === 'backoff'    ? '#e89a1b' :
            k === 'homing'     ? '#3a7cc4' :
            k === 'fault'      ? '#c33333' :
            k === 'estop'      ? '#c33333' :
            k === 'driver_off' ? '#777'    :
                                 '#555'
        );
        el.style.background = c;
        el.style.color      = '#fff';
    }

    function $(id)              { return document.getElementById(id); }
    function setText(id, v)     { const el = $(id); if (el) el.textContent = (v === undefined || v === null) ? '--' : v; }

    // Refresh the accordion-header "summary tags" so the user sees the key
    // settings of each card without expanding it (Elyon-style hint badges).
    function refreshSummaries(fix) {
        const u = _unit;
        const toDisp = (cm) => (u === 'in' ? (cm/2.54) : cm).toFixed(1) + ' ' + u;
        setText('oSumDmx', 'P' + (fix.personality || 1) + ' · pos@' + (fix.positionStart || 1) + ' · ctl@' + (fix.controlStart || 3));
        setText('oSumHome', (fix.homingDirection < 0 ? 'down (-)' : 'up (+)'));
        const jogSpd = stepsToCm(fix.jogSpeed || 0);
        setText('oSumJog', toDisp(jogSpd) + '/s');
        const dn = stepsToCm(fix.downPosition || 0);
        const up = stepsToCm(fix.upPosition   || 0);
        setText('oSumTravel', toDisp(dn) + ' → ' + toDisp(up));
        const haveProfile = fix.sgProfile && fix.sgProfile.valid;
        setText('oSumCal', 'SGTHRS=' + (fix.operSgthrs || '--') + (haveProfile ? ' · profile ✓' : ' · profile —'));
        setText('oSumSafety', ['E-stop','Return home'][fix.dmxWatchdogAction || 0] || '');
    }

    // Pulse highlight on Home button + "homing" badge while motor unhomed.
    function updateHomingHint(homed) {
        const btn = $('oHomeBtn');
        if (!btn) return;
        btn.classList.toggle('pulse-hint', !homed);
        btn.title = homed ? 'Re-home the rig' : 'First step — run homing before anything else';
    }

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
        1: '1 — Basic (4 ch, 8-bit position)',
        2: '2 — Basic HD (5 ch, 16-bit position)',
        3: '3 — Standard (6 ch, 16-bit + accel)',
    };
    const WD_ACTIONS = {
        0: 'E-stop',
        1: 'Return to home',
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
        const runCurrentMa  = num(fix.runCurrentMa,  500);
        const holdCurrentMa = num(fix.holdCurrentMa,  50);

        let h = '';
        h += '<div class="acc-wrap"><div class="acc-body open"><div class="acc-inner">';

        // ── Status panel + action buttons ────────────────────────────────────
        h += '<div id="orionStatus" style="display:flex;flex-wrap:wrap;gap:10px;align-items:center;font-size:12px;color:var(--txt2)">';
        h += '  <span id="orionState" style="padding:4px 10px;border-radius:6px;background:#444;color:#fff;font-weight:600;font-size:11px;letter-spacing:.04em;text-transform:uppercase">--</span>';
        h += '  <span>pos: <b id="orionPosCm" style="color:var(--txt)">--</b> <span id="orionPosUnit">cm</span></span>';
        h += '  <span>sg: <b id="orionSg" style="color:var(--txt)">--</b></span>';
        h += '  <span id="orionHomed" style="color:var(--txt3)">not homed</span>';
        h += '  <span id="orionFaults" style="color:#c44"></span>';
        h += '</div>';

        h += '<div style="display:flex;gap:6px;margin-top:8px;flex-wrap:wrap">';
        h += '  <button type="button" class="act-btn" id="oHomeBtn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionPost(\'/home\')">Home</button>';
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
        h += '  <div class="field"><label class="lbl">Position start</label>';
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
        h += '<p class="field-note">Sensorless homing via StallGuard. First action after power-up — run this before anything else. If your rig has no end stop, use "Set as home" in the action bar to capture the current position as zero.</p>';
        h += '<div class="field"><label class="lbl">Direction</label>';
        h += '  <select id="oHomingDir">';
        h += opt('-1', 'Down (-)', homingDir === -1);
        h += opt('1',  'Up (+)',   homingDir === 1);
        h += '  </select>';
        h += '</div>';
        h += '<p class="field-note">Direction the motor turns to find the end stop. Tune in the Calibration card below if homing trips spuriously.</p>';
        h += '<div class="div" style="margin:8px 0"></div>';
        h += '<span class="lbl">Manual override</span>';
        h += '<p class="field-note">If there is no mechanical end stop, jog the motor where you want zero to be and click below to capture it as home.</p>';
        h += '<button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px;margin-top:4px" onclick="orionSetHome()">Set as home</button>';
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
        h += '<p class="field-note">When on, jog ignores the saved DOWN/UP limits — use to drive past them and then click below to capture the new position.</p>';
        h += '<div class="div" style="margin:6px 0"></div>';
        h += '<span class="lbl">Capture current position as soft limit</span>';
        h += '<div style="display:flex;gap:6px;margin-top:6px">';
        h += '  <button type="button" class="act-btn" style="flex:1;border-radius:var(--r);padding:9px;font-size:12px" onclick="orionSetLimit(\'down\')">Set as DOWN limit</button>';
        h += '  <button type="button" class="act-btn" style="flex:1;border-radius:var(--r);padding:9px;font-size:12px" onclick="orionSetLimit(\'up\')">Set as UP limit</button>';
        h += '</div>';
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
        h += '<p class="field-note">Travel: <b id="orionTravelCm">--</b> <span class="o-u">cm</span></p>';
        h += '<p class="field-note">To capture new limits jog the motor to the desired position and use the buttons in the <b>Manual jog</b> card.</p>';
        h += '<div class="div" style="margin:8px 0"></div>';
        h += '<div class="tog-row">';
        h += '  <input type="checkbox" id="orionUnit"' + (_unit === 'in' ? ' checked' : '') + ' onchange="orionUnitChange()">';
        h += '  <span class="tog-lbl">Display in inches (off = cm)</span>';
        h += '</div>';
        h += cardClose();

        // ── 5. Calibration ───────────────────────────────────────────────────
        h += cardOpen('Calibration', 'oSumCal');
        h += '<span class="lbl">Mechanical calibration</span>';
        h += '<div class="g2" style="margin-top:4px">';
        h += '  <div class="field"><label class="lbl">Drum diameter (mm)</label>';
        h += '    <input type="number" id="oDrum" min="1" value="' + drumDiameter + '"></div>';
        h += '  <div class="field"><label class="lbl">Steps per revolution</label>';
        h += '    <input type="number" id="oStepsRev" min="1" value="' + stepsPerRev + '"></div>';
        h += '</div>';
        h += '<div class="field"><label class="lbl">Gear ratio</label>';
        h += '  <input type="number" id="oGear" min="1" step="0.01" value="' + gearRatio + '"></div>';
        h += '<p class="field-note">Drum + gear + steps/rev convert your cm values into stepper steps. Re-key these whenever you change the mechanical setup.</p>';

        h += '<div class="div" style="margin:10px 0"></div>';
        h += '<button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionStartSetupWizard()">▶ Run setup wizard</button>';
        h += '<p class="field-note">Guided 5-step procedure: homing direction → run homing → travel limits → StallGuard cal → profile sweep.</p>';

        h += '<div class="div" style="margin:10px 0"></div>';
        h += '<span class="lbl">StallGuard threshold (homing)</span>';
        h += '<p class="field-note" style="margin-top:2px">Single SG threshold used when looking for the mechanical end stop. Tune with the wizard against the real hung load.</p>';
        h += '<div class="field" style="margin-top:4px"><label class="lbl">SGTHRS</label>';
        h += '  <input type="number" id="oSgthrs" min="0" max="255" value="' + sgthrs + '"></div>';
        h += '<p class="field-note">Live SG: <b id="oSgthVal">--</b></p>';
        h += '<button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionCalOpen()">Calibrate StallGuard (homing)</button>';

        h += '<div class="div" style="margin:10px 0"></div>';
        h += '<span class="lbl">Adaptive SG profile (jog + DMX)</span>';
        h += '<p class="field-note" style="margin-top:2px">Speed- and direction-aware baseline of SG_RESULT. Once captured, real stalls during normal operation trip a fault automatically. Re-run after any mechanical change.</p>';
        h += '<button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionSgProfileOpen()">Profile sweep wizard</button>';
        h += cardClose();

        // ── 6. Motor tuning ──────────────────────────────────────────────────
        h += cardOpen('Motor tuning', 'oSumMotor');
        h += '<div class="g2">';
        h += '  <div class="field"><label class="lbl">Run current (mA)</label>';
        h += '    <input type="number" id="oRunCurrent" min="100" max="3000" step="50" value="' + runCurrentMa + '"></div>';
        h += '  <div class="field"><label class="lbl">Hold current (mA)</label>';
        h += '    <input type="number" id="oHoldCurrent" min="0" max="1000" step="10" value="' + holdCurrentMa + '"></div>';
        h += '</div>';
        h += '<p class="field-note">Set <b>Run current</b> to the motor\'s rated RMS current (from its datasheet). Do not exceed it — the TMC2209 will overheat the winding. <b>Hold current</b> is the standstill current; 5–15% of run current keeps the winch locked while staying near-silent and cool.</p>';
        h += cardClose();

        // ── 7. Safety ────────────────────────────────────────────────────────
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

        // Apply unit display + initial channel-map render + summary tags
        orionChMap();
        orionUnitChange();
        refreshSummaries(fix);
        updateHomingHint(!!fix.homed /* lifecycle reads from poll below */);
        // First-run onboarding: if any of the critical setup steps are
        // missing, offer the guided modal once per browser session.
        maybeShowOnboard(fix);

        // Live polling — kick off after render. Clear any previous timer so a
        // re-render (e.g. after factory reset) doesn't spawn parallel pollers.
        // Skip ticks when the tab is hidden to avoid background traffic on the
        // ESP32, and keep the rate at 1.5 s — fast enough for live readouts
        // without piling onto the shared /api/status poll from app.js.
        if (_statusTimer) clearInterval(_statusTimer);
        pollStatus();
        _statusTimer = setInterval(() => {
            if (document.visibilityState === 'visible') pollStatus();
        }, 3000);
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
            rows.push([ps,     'Position (16-bit high byte)']);
            rows.push([ps + 1, 'Position (16-bit low byte)']);
        }
        rows.push([cs,     'Enable (0=disarm/stop, ≥1=armed)']);
        rows.push([cs + 1, 'Speed']);
        if (p === 3) {
            rows.push([cs + 2, 'Acceleration']);
            rows.push([cs + 3, 'Function']);
        } else {
            rows.push([cs + 2, 'Function']);
        }
        rows.sort((a, b) => a[0] - b[0]);
        let html = rows.map(r =>
            '<div style="display:flex;gap:10px;font-size:12px;padding:4px 0;border-bottom:1px solid var(--line)">' +
            '<span style="color:var(--acc);font-weight:600;min-width:38px">CH' + r[0] + '</span>' +
            '<span style="color:var(--txt2)">' + r[1] + '</span></div>').join('');
        // Function byte sub-map: ranges only matter on that one channel.
        html += '<div style="margin-top:8px;font-size:11px;color:var(--txt3)">Function byte ranges:</div>';
        const funcMap = [
            ['0–49',    'Idle / clear pending'],
            ['50–99',   'Trigger homing  (Enable=0, hold 5 s)'],
            ['100–149', 'Clear fault'],
            ['150–199', 'Set DOWN limit  (Enable=0, hold 5 s)'],
            ['200–255', 'Set UP limit    (Enable=0, hold 5 s)'],
        ];
        html += funcMap.map(r =>
            '<div style="display:flex;gap:10px;font-size:11px;padding:3px 0;border-bottom:1px dotted var(--line)">' +
            '<span style="color:var(--txt2);min-width:64px">' + r[0] + '</span>' +
            '<span style="color:var(--txt3)">' + r[1] + '</span></div>').join('');
        el.innerHTML = html;
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

    // ── Guided setup wizard ──────────────────────────────────────────────────
    // 5-step checklist with live status polling. Shown automatically when
    // orionConfig.setupComplete = false, also explicitly via the
    // "Run setup wizard" button in the Calibration card. Each step
    // self-checks against /motorstatus + /api/config; ✓ marks completed
    // ones so the operator can pick up where they left off after closing.

    let _wiz = null;  // {timer}
    // Every wizard open is a redo-from-scratch: snapshot at first render,
    // each step is "done" only when its value diverges from baseline (or,
    // for mechanical/sweep, when the operator triggers the action here).
    let _wizRedoBaseline           = null;
    let _wizMechSavedThisSession   = false;
    let _wizSweepStartedThisSession = false;

    // Auto-open removed by design: it was popping on every page reload
    // because setupComplete is set late and storage-flags weren't sticky
    // enough across browsers. The wizard now only opens via the explicit
    // "Run setup wizard" button in the Calibration card.
    function maybeShowOnboard(/*fix*/) { /* no-op */ }

    window.orionStartSetupWizard = function () {
        // Every manual entry is a redo: snapshot the current state as
        // baseline and only flip a step to ✓ when the operator actually
        // re-runs it in this session.
        _wizMechSavedThisSession = false;
        _wizSweepStartedThisSession = false;
        _wizRedoBaseline = null;
        openWizardModal();
    };

    function openWizardModal() {
        let m = $('orionOnboardModal');
        if (!m) {
            m = document.createElement('div');
            m.id = 'orionOnboardModal';
            m.className = 'modal-overlay';
            document.body.appendChild(m);
        }
        m.classList.add('open');
        renderWizardSteps();
        if (_wiz && _wiz.timer) clearInterval(_wiz.timer);
        // Poll while the modal is open so the operator sees ✓ flip in real time
        // after they close a sub-wizard or click Save.
        _wiz = {timer: setInterval(renderWizardSteps, 1500)};
    }

    window.orionOnboardClose = function () {
        if (_wiz && _wiz.timer) clearInterval(_wiz.timer);
        _wiz = null;
        _wizRedoMode = false;
        _wizRedoBaseline = null;
        const m = $('orionOnboardModal');
        if (m) m.classList.remove('open');
    };

    window.orionOnboardSkip = function () { orionOnboardClose(); };

    window.recheckWizardStep = function () { renderWizardSteps(); };

    window.orionOnboardFinish = function () {
        // Persist setupComplete = true so the modal never auto-opens again.
        fetch('/api/config').then(r => r.json()).then(cfg => {
            if (!cfg.fixture) cfg.fixture = {};
            cfg.fixture.setupComplete = true;
            return fetch('/api/config', {
                method: 'POST', headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(cfg)
            });
        }).then(() => {
            orionOnboardClose();
            showToast('Setup complete — saved');
        }).catch(() => showToast('Setup save failed'));
    };

    async function renderWizardSteps() {
        const m = $('orionOnboardModal');
        if (!m || !m.classList.contains('open')) return;
        let s = {}, cfg = {};
        try { s = await (await fetch('/motorstatus')).json(); } catch (e) {}
        try { const c = await (await fetch('/api/config')).json(); cfg = c.fixture || {}; } catch (e) {}

        if (!_wizRedoBaseline) {
            _wizRedoBaseline = {
                drum: cfg.drumDiameterMm, spr: cfg.motorStepsPerRev, gear: cfg.gearRatio,
                homingDirection: cfg.homingDirection,
                upCm: s.upCm, downCm: s.downCm,
                profileValid: !!(cfg.sgProfile && cfg.sgProfile.valid),
                homed: !!s.homed,
            };
        }
        const b = _wizRedoBaseline;

        const mechDone   = _wizMechSavedThisSession;
        const dirDone    = (cfg.homingDirection !== b.homingDirection)
                           && (cfg.homingDirection === -1 || cfg.homingDirection === 1);
        const homeDone   = !!s.homed && !b.homed;
        const limitsDone = (Math.abs((s.upCm || 0) - (b.upCm || 0)) > 0.5)
                           || (Math.abs((s.downCm || 0) - (b.downCm || 0)) > 0.5);
        const profileDone = !!(cfg.sgProfile && cfg.sgProfile.valid)
                            && (!b.profileValid || _wizSweepStartedThisSession);

        const isHoming    = (s.state === 'homing');
        const isSweeping  = (s.state === 'sweep' || s.state === 'sweeping');
        const drumV = (cfg.drumDiameterMm   != null) ? cfg.drumDiameterMm   : 50;
        const sprV  = (cfg.motorStepsPerRev != null) ? cfg.motorStepsPerRev : 200;
        const gearV = (cfg.gearRatio        != null) ? cfg.gearRatio        : 1;
        const dirV  = cfg.homingDirection;
        const u = (_unit === 'in') ? 'in' : 'cm';
        const posDispCm = (s.posCm  != null) ? s.posCm  : 0;
        const upDispCm  = (s.upCm   != null) ? s.upCm   : 0;
        const dnDispCm  = (s.downCm != null) ? s.downCm : 0;
        const cv = (cm) => (u === 'in' ? (cm/2.54).toFixed(2) : cm.toFixed(2));

        const stepBox = (done, label, body) => {
            const status = done ? '✓' : '•';
            const color  = done ? '#2e9c4a' : 'var(--acc)';
            return '<div style="border-top:1px solid var(--line);padding:12px 0;opacity:' + (done ? '0.6' : '1') + '">'
                 + '  <div style="display:flex;align-items:center;gap:8px;margin-bottom:8px">'
                 + '    <span style="color:' + color + ';font-weight:700;font-size:14px;width:14px">' + status + '</span>'
                 + '    <span style="font-weight:600">' + label + '</span>'
                 + '  </div>'
                 + '  <div style="margin-left:22px">' + body + '</div>'
                 + '</div>';
        };

        const numIn = (id, val, attrs) => '<input id="' + id + '" type="number" value="' + val + '" ' + (attrs || '') + ' style="width:80px;padding:5px;border-radius:6px;border:1px solid var(--line);background:#1c1c1f;color:#eee">';
        const btn = (label, onclick, opts) => {
            opts = opts || {};
            const dis = opts.disabled ? ' disabled' : '';
            const extra = opts.style || '';
            return '<button type="button" class="act-btn" style="border-radius:var(--r);padding:7px 12px;font-size:12px;' + extra + '"' + dis + ' onclick="' + onclick + '">' + label + '</button>';
        };

        // Step 1 — Mechanical setup
        const mechBody =
              '<p class="field-note" style="margin:0 0 8px">Drum diameter, motor steps/rev and gear ratio drive cm conversion. Set these first.</p>'
            + '<div style="display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-bottom:8px">'
            + '<label style="font-size:12px">Drum Ø (mm) ' + numIn('wizDrum', drumV, 'min="1" step="1"') + '</label>'
            + '<label style="font-size:12px">Steps/rev '   + numIn('wizSpr',  sprV,  'min="1" step="1"') + '</label>'
            + '<label style="font-size:12px">Gear ratio '  + numIn('wizGear', gearV, 'min="0.1" step="0.1"') + '</label>'
            + '</div>'
            + btn('Save mechanical', 'orionWizSaveMech()', {});

        // Step 2 — Homing direction
        const dirBody =
              '<p class="field-note" style="margin:0 0 8px">Pick the direction the motor moves to seek the end stop. Save to apply.</p>'
            + '<div style="display:flex;gap:8px;align-items:center;margin-bottom:8px">'
            + '<select id="wizDir" style="padding:6px;border-radius:6px;border:1px solid var(--line);background:#1c1c1f;color:#eee">'
            + '  <option value="-1"' + (dirV === -1 ? ' selected' : '') + '>Down (-)</option>'
            + '  <option value="1"'  + (dirV === 1  ? ' selected' : '') + '>Up (+)</option>'
            + '</select>'
            + btn('Save direction', 'orionWizSaveDir()', {})
            + '</div>';

        // Step 3 — Run homing
        const homeBody =
              '<p class="field-note" style="margin:0 0 8px">Uses fixed homing-SGTHRS (no calibration needed). Establishes position 0.</p>'
            + (isHoming
                ? '<div style="padding:7px 12px;border:1px solid var(--acc);border-radius:var(--r);font-size:12px;color:var(--acc)">Homing in progress…</div>'
                : btn(homeDone ? '✓ Run again' : 'Run Home', "orionPost('/home')", {}))
            + '<div style="font-size:11px;color:#999;margin-top:6px">Status: ' + (s.state || '—') + ' · homed=' + (s.homed ? 'yes' : 'no') + ' · ' + btn('Clear fault', "orionPost('/clearfault')", {style:'padding:3px 8px;font-size:11px'}) + '</div>';

        // Step 4 — Travel limits (inline jog + set buttons)
        const jogV = (cfg.jogSpeed != null) ? cv((cfg.jogSpeed / Math.max(1, (Math.PI*drumV/10) > 0 ? (sprV*16*gearV/(Math.PI*drumV/10)) : 1))) : '5';
        const limitsBody =
              '<p class="field-note" style="margin:0 0 8px">Jog the motor to the lowest safe position → Set DOWN. Then to the highest → Set UP. Soft limits are bypassed here.</p>'
            + '<div style="display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-bottom:8px">'
            + '<label style="font-size:12px">Jog speed (' + u + '/s) <input id="wizJogSpeed" type="number" value="' + jogV + '" min="0.5" step="0.5" style="width:70px;padding:5px;border-radius:6px;border:1px solid var(--line);background:#1c1c1f;color:#eee"></label>'
            + '<button type="button" class="act-btn" style="border-radius:var(--r);padding:7px 12px;font-size:12px" onmousedown="orionWizJog(1)" onmouseup="orionJogStop()" ontouchstart="orionWizJog(1)" ontouchend="orionJogStop()">▲ Jog Up</button>'
            + '<button type="button" class="act-btn" style="border-radius:var(--r);padding:7px 12px;font-size:12px" onmousedown="orionWizJog(-1)" onmouseup="orionJogStop()" ontouchstart="orionWizJog(-1)" ontouchend="orionJogStop()">▼ Jog Down</button>'
            + '</div>'
            + '<div style="display:flex;flex-wrap:wrap;gap:8px;margin-bottom:6px">'
            + btn('Set as DOWN limit', "orionSetLimit('down')", {})
            + btn('Set as UP limit',   "orionSetLimit('up')",   {})
            + '</div>'
            + '<div style="font-size:11px;color:#999">Position ' + cv(posDispCm) + ' ' + u + ' · DOWN ' + cv(dnDispCm) + ' ' + u + ' · UP ' + cv(upDispCm) + ' ' + u + '</div>';

        // Step 5 — Profile sweep (auto-derives operational SGTHRS at the end)
        const swPhase = s.sgSweepPhase | 0;
        const sweepBody =
              '<p class="field-note" style="margin:0 0 8px">Captures SG profile across 8 speed bins × 2 directions (~30 s). At the end, operational SGTHRS is derived automatically from the data.</p>'
            + (isSweeping || swPhase === 1
                ? '<div style="padding:7px 12px;border:1px solid var(--acc);border-radius:var(--r);font-size:12px;color:var(--acc)">Sweep running… bin ' + (s.sgSweepBin || 0) + '/' + (s.sgSweepTotal || 8) + '</div>'
                : btn(profileDone ? '✓ Run sweep again' : 'Start profile sweep',
                       'orionWizStartSweep()',
                       { disabled: !s.homed }))
            + (!s.homed ? '<div style="font-size:11px;color:#c66;margin-top:6px">Requires homing first.</div>' : '');

        let html = '<div class="modal-box" style="text-align:left;max-width:580px;max-height:90vh;overflow-y:auto">';
        html += '  <h3>Setup wizard</h3>';
        html += '  <p class="field-note">Each step shows the controls inline — no need to switch cards. Close at any time and reopen from <b>Calibration → Run setup wizard</b>.</p>';
        html += stepBox(mechDone,   '1 · Mechanical setup',  mechBody);
        html += stepBox(dirDone,    '2 · Homing direction',  dirBody);
        html += stepBox(homeDone,   '3 · Run homing',        homeBody);
        html += stepBox(limitsDone, '4 · Travel limits',     limitsBody);
        html += stepBox(profileDone,'5 · Profile sweep',     sweepBody);

        const allDone = mechDone && dirDone && homeDone && limitsDone && profileDone;
        html += '<div style="margin-top:12px;display:flex;gap:8px;justify-content:flex-end">';
        if (allDone) {
            html += btn('✓ Done — save & close', 'orionOnboardFinish()',
                       {style:'background:var(--acc);color:#111;border-color:var(--acc);font-weight:600;padding:8px 14px'});
        } else {
            html += '<button type="button" class="modal-cancel" onclick="orionOnboardClose()">Close (resume later)</button>';
        }
        html += '</div></div>';
        m.innerHTML = html;
    }

    // ── Wizard inline action handlers ───────────────────────────────────────
    window.orionWizSaveMech = async function () {
        const drum = parseFloat($('wizDrum') && $('wizDrum').value) || 50;
        const spr  = parseInt  ($('wizSpr')  && $('wizSpr').value)  || 200;
        const gear = parseFloat($('wizGear') && $('wizGear').value) || 1;
        try {
            const r = await fetch('/api/config');
            const cfg = await r.json();
            if (!cfg.fixture) cfg.fixture = {};
            cfg.fixture.drumDiameterMm   = drum;
            cfg.fixture.motorStepsPerRev = spr;
            cfg.fixture.gearRatio        = gear;
            await fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(cfg)});
            _wizMechSavedThisSession = true;
            showToast('Mechanical saved');
            renderWizardSteps();
        } catch (e) { showToast('Mechanical save failed'); }
    };

    window.orionWizSaveDir = async function () {
        const dir = parseInt($('wizDir') && $('wizDir').value) || -1;
        try {
            const r = await fetch('/api/config');
            const cfg = await r.json();
            if (!cfg.fixture) cfg.fixture = {};
            cfg.fixture.homingDirection = dir;
            await fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(cfg)});
            showToast('Direction saved');
            renderWizardSteps();
        } catch (e) { showToast('Direction save failed'); }
    };

    window.orionWizJog = function (dir) {
        // Wizard jog always forces override so the operator can drive past
        // the previously-saved soft limits to define new ones.
        const v = parseFloat($('wizJogSpeed') && $('wizJogSpeed').value) || 5;
        const cm = (_unit === 'in') ? v * 2.54 : v;
        const speed = cmToSteps(cm) || 1000;
        const fd = new FormData();
        fd.append('dir',      dir > 0 ? 'up' : 'down');
        fd.append('speed',    speed);
        fd.append('override', '1');
        fetch('/jog', {method:'POST', body: fd}).catch(() => {});
    };

    window.orionWizStartSweep = function () {
        _wizSweepStartedThisSession = true;
        // The dedicated sweep modal has the full 2-step (up + down) flow
        // and progress display. Close the setup wizard and open the sweep
        // wizard — once the profile is saved (sgProfile.valid=true), the
        // operator can reopen "Run setup wizard" and step 5 will be ✓.
        orionOnboardClose();
        window.orionSgProfileOpen && window.orionSgProfileOpen();
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
            let cappedNoted = false;
            _swp.timer = setInterval(async () => {
                const p = await (await fetch('/sgsweep')).json();
                setText('oSwpBin', p.currentBin);
                setText('oSwpSg',  p.lastSg);
                const pct = (p.currentBin / p.totalBins) * 100;
                const fill = $('oSwpFill');
                if (fill) fill.style.width = pct + '%';
                // Warn once if the device had to cap the step to fit the travel.
                if (p.capped && !cappedNoted) {
                    cappedNoted = true;
                    const topBin = p.baseSpeed + 7 * p.speedStep;
                    setText('oSwpStatus', '⚠ Travel too short — top bin capped at ' + topBin + ' step/s (no stall coverage above)');
                }
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
                        const cappedHint = p.capped ? ' (stall above ' + (p.baseSpeed + 7*p.speedStep) + ' step/s uncovered — consider a longer travel rig for full coverage)' : '';
                        setText('oSwpStatus', '✓ Profile saved.' + cappedHint);
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
            runCurrentMa:      parseInt(getV('oRunCurrent'))    || 500,
            holdCurrentMa:     parseInt(getV('oHoldCurrent'))   || 50,
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
