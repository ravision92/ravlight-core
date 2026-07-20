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

    // Backend hard caps for motion params — fetched once from /api/motor-limits.
    // Falls back to conservative values if the fetch hasn't landed yet (input
    // max attribute is set at render time; a late arrival is patched onto any
    // existing DOM inputs afterwards). Values are in steps/s (or steps/s²).
    let _motorLimits = { maxSpeedSteps: 25000, maxAccelSteps: 60000, maxJogSteps: 15000 };
    fetch('/api/motor-limits').then(r => r.json()).then(j => {
        if (j && j.maxSpeedSteps) _motorLimits = j;
        // Retro-patch already-rendered inputs so the browser enforces the max
        // attribute even if the render happened before the fetch resolved.
        const patch = (id, cap) => {
            const el = document.getElementById(id);
            if (el) el.max = ((_unit === 'in' ? cap / 2.54 : cap) / (stepsPerCmSafe() || 1)).toFixed(2);
        };
        patch('oMaxSpeed', j.maxSpeedSteps);
        patch('oMaxAccel', j.maxAccelSteps);
        patch('oJogSpeed', j.maxJogSteps);
    }).catch(() => {});
    // Fallback stepsPerCm accessor for the pre-render patch path. Uses the
    // main-page mechanical inputs if present, else 1 (no scaling).
    function stepsPerCmSafe() {
        const spr  = parseInt(document.getElementById('oStepsRev') && document.getElementById('oStepsRev').value)   || 200;
        const drum = parseFloat(document.getElementById('oDrum')    && document.getElementById('oDrum').value)     || 30;
        const gear = parseFloat(document.getElementById('oGear')    && document.getElementById('oGear').value)     || 1;
        const cir_cm = (Math.PI * drum) / 10;
        return (spr * 16 * gear) / cir_cm;
    }

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
            // Positive-range travel visualisation (home / far / DMX + live pos).
            updateTravelViz(s);
            // Fault flags
            const fbox = $('orionFaults');
            if (fbox) {
                const ff = s.faultFlags || 0;
                if (ff) {
                    fbox.textContent = 'FAULT: ' + FAULT_LABELS.filter(f => ff & f.bit).map(f => f.label).join(', ');
                } else fbox.textContent = '';
            }
            // Manual mode badge + Home button gating
            const badge  = $('orionManualBadge');
            const mm     = !!s.manualMode;
            if (badge) badge.style.display = mm ? '' : 'none';
            // Big Home button follows the live manual state: "Set home" (manual)
            // vs "Home" (sensorless run).
            if (window.updateHomeBtn) updateHomeBtn(mm, !!s.homed);
            const mmChk = $('oManualToggle');
            if (mmChk && mmChk.checked !== mm) mmChk.checked = mm;
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
    // Tag IDs match the new card layout: DMX, Motion, Homing, Travel, Jog,
    // Stall detection, Setup.
    function refreshSummaries(fix) {
        const u = _unit;
        const toDisp = (cm) => (u === 'in' ? (cm/2.54) : cm).toFixed(1) + ' ' + u;

        // DMX — patch + loss action
        const wdLabel = ['E-stop','Return home','Do nothing'][fix.dmxWatchdogAction || 0] || 'E-stop';
        setText('oSumDmx',
            'P' + (fix.personality || 1)
            + ' · pos@' + (fix.positionStart || 1)
            + ' · ctl@' + (fix.controlStart || 3)
            + ' · loss=' + wdLabel.toLowerCase());

        // Motion — kinematic envelope + currents
        const sp   = stepsToCm(fix.maxSpeed || 0);
        const ac   = stepsToCm(fix.maxAccel || 0);
        const jgSp = stepsToCm(fix.jogSpeed || 0);
        setText('oSumMotion',
            toDisp(sp) + '/s · ' + toDisp(ac) + '/s² · jog ' + toDisp(jgSp) + '/s'
            + ' · ' + (fix.runCurrentMa || '--') + '/' + (fix.holdCurrentMa || '--') + ' mA');

        // Homing — direction + auto-recovery bits
        const homeDirTag = (fix.homingDirection < 0 ? 'down (-)' : 'up (+)');
        const hBits = [homeDirTag];
        if (fix.homeAtBoot)        hBits.push('auto-boot');
        if (fix.autoRehomeOnStall) hBits.push('auto-rehome');
        if (fix.dropAndRehome)     hBits.push('drop-rehome');
        setText('oSumHome', hBits.join(' · '));

        // Jog & Travel — the persistent travel range is what belongs on the
        // summary since jog itself has no persistent state.
        const dn = stepsToCm(fix.downPosition || 0);
        const up = stepsToCm(fix.upPosition   || 0);
        setText('oSumJog', toDisp(dn) + ' → ' + toDisp(up));

        // Stall detection — SGTHRS + profile status + σ + manual mode
        const haveProfile = fix.sgProfile && fix.sgProfile.valid;
        const sBits = ['SGTHRS=' + (fix.operSgthrs || '--')];
        sBits.push(haveProfile ? 'profile ✓' : 'profile —');
        if (fix.sgConfidenceSigma && fix.sgConfidenceSigma !== 3) sBits.push(fix.sgConfidenceSigma + 'σ');
        if (fix.manualMode) sBits.push('⚠ manual');
        setText('oSumStall', sBits.join(' · '));

        // Setup — mechanical fingerprint
        setText('oSumSetup',
            'Ø' + (fix.drumDiameterMm || '--') + 'mm · '
            + (fix.motorStepsPerRev || '--') + ' step/rev · '
            + (fix.gearRatio || 1) + ':1');
    }

    // Title hint on the big Home button depending on homed state (it already
    // pulses via the .ohome animation).
    function updateHomingHint(homed) {
        const btn = $('oHomeBig');
        if (!btn) return;
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
        2: 'Do nothing',
    };

    // Winch visualisation styles (from the design mockup). Injected inline so
    // the block is self-contained in fixture.js — no shared style.css edits.
    // Responsive: everything is anchored to the vertical centre line (50%) so
    // the winch stays centred and the flanking labels/DMX ticks adapt to the
    // form width. JS only drives the vertical (top) positions.
    const ORION_VIZ_CSS = '<style>'
      + '.ovwrap{position:relative;height:100%;min-height:300px;margin:0}'
      + '.ovtrack{position:absolute;left:50%;transform:translateX(-50%);top:18px;bottom:18px;width:4px;background:var(--s5);border-radius:2px}'
      + '.ovband{position:absolute;left:50%;transform:translateX(-50%);width:4px;border-radius:2px}'
      + '.ovdrum{position:absolute;left:50%;transform:translateX(-50%);width:30px;height:30px;border-radius:50%;background:var(--s3);border:2px solid var(--acc-dim);display:flex;align-items:center;justify-content:center;transition:top .3s;z-index:2}'
      + '.ovdrum:after{content:"";width:10px;height:10px;border-radius:50%;background:var(--acc)}'
      + '.ovcable{position:absolute;left:50%;transform:translateX(-50%);width:2px;background:var(--txt4);transition:top .3s,height .3s}'
      + '.ovload{position:absolute;left:50%;transform:translateX(-50%);width:42px;height:13px;border-radius:3px;background:#3ddc84;box-shadow:0 0 0 2px var(--s2),0 0 12px rgba(61,220,132,.5);transition:top .3s;z-index:3}'
      + '.ovload.oob{background:#ff9c3d;box-shadow:0 0 0 2px var(--s2),0 0 12px rgba(255,156,61,.5)}'
      + '.ovmark{position:absolute;right:calc(50% + 30px);max-width:40%;display:flex;align-items:center;justify-content:flex-end;gap:7px;transition:top .3s}'
      + '.ovmark .l{text-align:right}'
      + '.ovmark .l b{display:block;font-size:12px;font-weight:700;line-height:1.15;white-space:nowrap}'
      + '.ovmark .l s{text-decoration:none;color:var(--txt3);font-size:9px}'
      + '.ovmark .d{width:9px;height:9px;border-radius:50%;flex:none}'
      + '.ovarrow{position:absolute;left:calc(50% + 28px);color:var(--acc);font-size:14px;transition:top .3s}'
      + '.ovtick{position:absolute;left:calc(50% + 44px);max-width:38%;font-size:10px;color:#5ab0ff}'
      + '.ovtick b{font-weight:700;white-space:nowrap}'
      + '.ovtick s{text-decoration:none;color:var(--txt3);display:block;font-size:9px}'
      // hub grid: diagram (left) + primary controls (right); stacks on narrow
      + '.ovhub{display:grid;grid-template-columns:212px 1fr;gap:12px;align-items:start}'
      + '@media(max-width:480px){.ovhub{grid-template-columns:1fr}}'
      + '.ovctl{display:flex;flex-direction:column;gap:10px;min-width:0}'
      + '.ovctl .sub{font-size:9px;text-transform:uppercase;letter-spacing:.08em;color:var(--txt3);font-weight:700;margin-bottom:4px;display:flex;align-items:center;gap:5px}'
      + '.ovctl .jogpad{display:flex;gap:6px}'
      + '.ovctl .jogpad button{flex:1;padding:11px;font-size:14px;font-family:inherit;cursor:pointer;background:var(--s3);border:1px solid var(--line2);color:var(--txt);border-radius:var(--r);user-select:none}'
      + '.ovctl .jogpad button:active{background:var(--acc);color:#000;border-color:var(--acc)}'
      + '.ovctl select,.ovctl input[type=number]{width:100%;background:var(--s2);border:1px solid var(--line2);color:var(--txt);border-radius:6px;padding:7px 9px;font-family:inherit;font-size:12px}'
      + '.ovctl input[type=number]{text-align:right}'
      + '.ovbtns{display:flex;gap:6px}'
      + '.ovbtns button{flex:1;padding:8px 4px;font-size:11px;font-family:inherit;cursor:pointer;border-radius:var(--r);background:transparent;border:1px solid var(--line2);color:var(--acc)}'
      + '.ovtogrow{display:flex;align-items:center;justify-content:space-between;gap:8px;font-size:12px;color:var(--txt2)}'
      // all checkboxes inside the hub render as slider switches (not flags)
      + '.ovctl input[type=checkbox]{-webkit-appearance:none;appearance:none;width:40px;height:22px;border-radius:12px;background:var(--s5);border:1px solid var(--line2);position:relative;cursor:pointer;flex:none;margin:0}'
      + '.ovctl input[type=checkbox]::after{content:"";position:absolute;top:2px;left:2px;width:16px;height:16px;border-radius:50%;background:var(--txt4);transition:left .2s,background .2s}'
      + '.ovctl input[type=checkbox]:checked{background:var(--acc-bg);border-color:var(--acc-dim)}'
      + '.ovctl input[type=checkbox]:checked::after{left:20px;background:var(--acc)}'
      + '.ovctl input#oJogOverride:checked{background:var(--orange);border-color:var(--orange)}'
      + '.ovctl input#oJogOverride:checked::after{background:#fff}'
      + '.ovctl input#oJogOverride:checked::after{background:var(--far)}'
      // segmented selectors (homing mode, jog speed)
      + '.ovseg{display:flex;gap:5px}'
      + '.ovseg button{flex:1;padding:7px 3px;font-size:10.5px;font-family:inherit;cursor:pointer;background:transparent;border:1px solid var(--line2);color:var(--txt2);border-radius:var(--r)}'
      + '.ovseg button.on{background:var(--acc);color:#000;border-color:var(--acc);font-weight:700}'
      // big Home button with the intermittent pulse
      + '.ohome{width:100%;padding:9px;border:none;border-radius:var(--r);background:var(--acc);color:#000;font-family:inherit;font-size:12px;font-weight:700;letter-spacing:.04em;text-transform:uppercase;cursor:pointer;animation:ohomepulse 1.8s ease-in-out infinite}'
      + '.ohome.set{background:var(--home)}'
      + '.ohome.homed{animation:none}'
      + '.ovfarin{width:56px;background:transparent;border:1px solid transparent;border-bottom:1px dashed var(--far);color:var(--far);font-family:inherit;font-size:13px;font-weight:700;text-align:right;padding:0 2px}'
      + '.ovfarin:focus{outline:none;border:1px solid var(--far);border-radius:4px;background:rgba(255,156,61,.08)}'
      + '#ovLive{position:absolute;left:2px;top:50%;transform:translateY(-50%);font-size:11px;color:var(--txt3);max-width:40%;line-height:1.3}'
      + '@keyframes ohomepulse{0%,100%{box-shadow:0 0 0 0 rgba(233,255,0,0)}50%{box-shadow:0 0 0 4px rgba(233,255,0,.20)}}'
      + '@media(prefers-reduced-motion:reduce){.ohome{animation:none}}'
      + '</style>';

    // Reusable accordion card open/close helpers — match the legacy ORION_HTML
    // collapsible sub-cards (.ch-card / .ch-head / .ch-body).
    function cardOpen(title, sumId, openByDefault) {
        // openByDefault renders the card expanded (adds .open + max-height:none)
        // so frequently-used cards are ready without a click; advanced cards
        // stay collapsed to keep the page quiet.
        let h = '<div class="ch-card' + (openByDefault ? ' open' : '') + '">';
        h += '  <div class="ch-head" style="grid-template-columns:1fr 24px" onclick="orionToggleCard(this)">';
        h += '    <div class="ch-sum">';
        h += '      <span class="ch-proto">' + title + '</span>';
        h += '      <div class="ch-tags"><span class="ch-tag" id="' + sumId + '"></span></div>';
        h += '    </div>';
        h += '    <span class="ch-arr">&#9661;</span>';
        h += '  </div>';
        h += '  <div class="ch-body"' + (openByDefault ? ' style="max-height:none"' : '') + '><div class="ch-form">';
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

        // Positive-range model: home = 0, Travel = the far-limit magnitude.
        // The far side depends on homing direction (home DOWN → far = up;
        // home UP → far = -down). Prefill Travel from the model-consistent far
        // limit so saving re-aligns a misaligned config (drops any erroneous
        // travel on the home side) WITHOUT moving the far end.
        const _homingDir0  = num(fix.homingDirection, -1);
        const _farSteps0   = _homingDir0 < 0 ? num(fix.upPosition, 0)
                                             : -num(fix.downPosition, 0);
        const travelDisp   = stepsToDisp(Math.max(0, _farSteps0));
        const maxSpeedDisp     = stepsToDisp(num(fix.maxSpeed,     6000));
        const maxAccelDisp     = stepsToDisp(num(fix.maxAccel,     8000));
        const jogSpeedDisp     = stepsToDisp(num(fix.jogSpeed,     2000));

        const homingDir     = num(fix.homingDirection, -1);
        const dmxWd         = num(fix.dmxWatchdogAction, 0);
        const sgthrs        = num(fix.operSgthrs,    100);
        const runCurrentMa     = num(fix.runCurrentMa,  500);
        const holdCurrentMa    = num(fix.holdCurrentMa,  50);
        const autoRehomeOnStall = !!(fix.autoRehomeOnStall);

        let h = '';
        h += '<div class="acc-wrap"><div class="acc-body open"><div class="acc-inner">';

        // ── Winch control card: status + actions + hub (diagram + controls) ──
        h += ORION_VIZ_CSS;
        h += '<div id="oHub" style="background:var(--s2);border:1px solid var(--line);border-radius:var(--rm);padding:10px 12px">';
        // status bar (inside the card)
        h += '  <div id="orionStatus" style="display:flex;flex-wrap:wrap;gap:6px 10px;align-items:center;font-size:11px;color:var(--txt2)">';
        h += '    <span id="orionState" style="padding:4px 10px;border-radius:6px;background:#444;color:#fff;font-weight:600;font-size:11px;letter-spacing:.04em;text-transform:uppercase">--</span>';
        h += '    <span>sg: <b id="orionSg" style="color:var(--txt)">--</b></span>';
        h += '    <span id="orionHomed" style="color:var(--txt3)">not homed</span>';
        h += '    <span id="orionFaults" style="color:#c44"></span>';
        h += '    <span id="orionManualBadge" style="display:none;padding:4px 10px;border-radius:6px;background:#c67c00;color:#fff;font-weight:600;font-size:11px;letter-spacing:.04em;text-transform:uppercase" title="Automatic safety features disabled. Operator responsible.">⚠ Manual</span>';
        h += '  </div>';
        // action buttons (inside the card)
        h += '  <div style="display:flex;gap:6px;margin-top:8px;flex-wrap:wrap">';
        h += '    <button type="button" id="oReleaseBtn" class="act-btn" style="border-radius:var(--r);padding:7px 6px;font-size:11px" onclick="orionPost(\'/release-dmx\')" disabled>Release to DMX</button>';
        h += '    <button type="button" class="act-btn" style="border-radius:var(--r);padding:7px 6px;font-size:11px" onclick="orionPost(\'/clearfault\')">Clear Fault</button>';
        h += '    <button type="button" class="act-btn" style="border-radius:var(--r);padding:7px 6px;font-size:11px;background:var(--red);color:#fff;border-color:var(--red);font-weight:700;letter-spacing:.05em" onclick="orionPost(\'/estop\')">E-STOP</button>';
        h += '    <button type="button" class="act-btn" style="border-radius:var(--r);padding:7px 6px;font-size:11px" onclick="orionPost(\'/highlight\')">Highlight</button>';
        h += '  </div>';
        h += '  <div style="height:8px"></div>';
        h += '  <div class="ovhub">';
        // ── left: winch diagram + live readout ──────────────────────────────
        h += '    <div>';
        h += '      <div class="ovwrap">';
        h += '        <div class="ovtrack"></div>';
        h += '        <div class="ovband" id="ovBand"></div>';
        h += '        <div class="ovdrum" id="ovDrum"></div>';
        h += '        <div class="ovcable" id="ovCable"></div>';
        h += '        <div class="ovload" id="ovLoad"></div>';
        h += '        <div class="ovmark" id="ovHome"><span class="l"><b style="color:#3ddc84">HOME &middot; 0</b><s>reference</s></span><span class="d" style="background:#3ddc84"></span></div>';
        h += '        <div class="ovmark" id="ovFar"><span class="l"><input type="number" id="oTravel" class="ovfarin" min="0" step="any" value="' + travelDisp + '" oninput="updateTravelViz()"><s>far &middot; cm</s></span><span class="d" style="background:#ff9c3d"></span></div>';
        h += '        <div class="ovarrow" id="ovArrow">&#9650;</div>';
        h += '        <div class="ovtick" id="ovDmxTop"></div>';
        h += '        <div class="ovtick" id="ovDmxBot"></div>';
        h += '        <div id="ovLive"></div>';
        h += '      </div>';
        h += '    </div>';
        // ── right: primary controls (jog, homing dir, travel, bypass, DMX) ──
        h += '    <div class="ovctl">';
        // units
        h += '      <div class="ovtogrow"><span class="sub" style="margin:0">Units</span>';
        h += '        <label style="display:flex;align-items:center;gap:6px;font-size:11px;color:var(--txt2)"><input type="checkbox" id="orionUnit"' + (_unit === 'in' ? ' checked' : '') + ' onchange="orionUnitChange()"> inches</label>';
        h += '      </div>';
        // Home button — mode-aware (Run home / Set home), pulses until homed.
        h += '      <button type="button" id="oHomeBig" class="ohome" onclick="orionHomeAction()">⌂ Run home</button>';
        h += '      <div><div class="sub">Homing direction</div>';
        h += '        <select id="oHomingDir" onchange="updateTravelViz()">' + opt('-1', 'Down (−)', homingDir === -1) + opt('1', 'Up (+)', homingDir === 1) + '</select>';
        h += '      </div>';
        // jog + speed buttons + bypass
        h += '      <div><div class="sub">Jog</div>';
        h += '        <div class="jogpad"><button type="button" onmousedown="orionJog(1)" onmouseup="orionJogStop()" ontouchstart="orionJog(1)" ontouchend="orionJogStop()">&#9650;</button><button type="button" onmousedown="orionJog(-1)" onmouseup="orionJogStop()" ontouchstart="orionJog(-1)" ontouchend="orionJogStop()">&#9660;</button></div>';
        h += '        <input type="hidden" id="oJogFactor" value="1">';
        h += '        <div class="ovseg" style="margin-top:6px"><button type="button" class="on" onclick="orionSetJogFactor(1,this)">Full</button><button type="button" onclick="orionSetJogFactor(0.5,this)">Half</button><button type="button" onclick="orionSetJogFactor(0.25,this)">&#188;</button></div>';
        h += '        <div class="ovtogrow" style="margin-top:8px"><span title="Ignores both soft limits and the home drum barrier">Bypass limits &#9888;</span><input type="checkbox" id="oJogOverride"></div>';
        h += '      </div>';
        // capture: set home / set far / reset (Travel value edited on the diagram)
        h += '      <div><div class="sub">Travel &amp; capture</div>';
        h += '        <div class="ovbtns"><button type="button" onclick="orionSetHome()">Set home</button><button type="button" onclick="orionSetFarHere()">Set limit</button><button type="button" onclick="orionResetTravel()">Reset &#8635;</button></div>';
        h += '      </div>';
        // invert DMX
        h += '      <div class="ovtogrow"><span>Invert DMX</span><input type="checkbox" id="oDmxInv"' + (_fix.dmxInvertPosition ? ' checked' : '') + ' onchange="updateTravelViz()"></div>';
        h += '    </div>';   // /ovctl
        h += '  </div>';     // /ovhub
        h += '</div>';       // /oHub

        h += '<div style="height:6px"></div>';
        h += '<div class="ch-list">';

        // Values pulled once for the cards that follow.
        const dropReh   = !!(fix.dropAndRehome);
        const dropWait  = num(fix.dropWaitMs, 3000);
        const sgSigma   = num(fix.sgConfidenceSigma, 3);

        // ── 1. DMX ───────────────────────────────────────────────────────────
        // Everything about the DMX input side: patch, direction inversion, and
        // what to do if the signal is lost (the reaction to a DMX event lives
        // here, not in a generic "safety" bucket).
        h += cardOpen('DMX', 'oSumDmx');
        h += '<div class="field"><label class="lbl">Personality</label>';
        h += '  <select id="oPersonality" onchange="orionAutoAddr()">';
        Object.keys(PERSONALITIES).forEach(k => h += opt(k, PERSONALITIES[k], Number(k) === personality));
        h += '  </select>';
        h += '</div>';
        h += '<div class="g2">';
        h += '  <div class="field"><label class="lbl">Position start</label>';
        h += '    <input type="number" id="oPositionStart" min="1" max="511" value="' + positionStart + '" oninput="orionAutoAddr()"></div>';
        h += '  <div class="field"><label class="lbl">Control start (Enable)</label>';
        h += '    <input type="number" id="oControlStart" min="1" max="510" value="' + controlStart + '" oninput="orionChMap()"></div>';
        h += '</div>';
        h += '<div class="div" style="margin:2px 0"></div>';
        h += '<span class="lbl">Channel map</span>';
        h += '<div id="orionChMap" style="margin-top:2px"></div>';
        h += '<p class="field-note">Position and Control are independent addresses inside the motor universe above. Invert DMX direction is in the control panel at the top.</p>';
        h += '<div class="div" style="margin:8px 0"></div>';
        h += '<div class="field"><label class="lbl">On DMX loss</label>';
        h += '  <select id="oWdAction">';
        Object.keys(WD_ACTIONS).forEach(k => h += opt(k, WD_ACTIONS[k], Number(k) === dmxWd));
        h += '  </select>';
        h += '</div>';
        h += '<p class="field-note">Watchdog action when incoming DMX stops. Manual mode overrides this — signal loss is ignored while manual mode is on.</p>';
        h += cardClose();

        // ── 2. Motion ────────────────────────────────────────────────────────
        // Envelope of how the motor moves: kinematic caps (speed, accel, jog)
        // and electrical parameters (run / hold current, read-only from wizard).
        // Grouped because tuning any of them without the others is misleading.
        h += cardOpen('Motion', 'oSumMotion');
        h += '<div class="g2">';
        h += '  <div class="field"><label class="lbl">Max speed (<span class="o-u">cm</span>/s)</label>';
        h += '    <input type="number" id="oMaxSpeed" step="any" min="0.1" max="' + stepsToDisp(_motorLimits.maxSpeedSteps) + '" value="' + maxSpeedDisp + '"></div>';
        h += '  <div class="field"><label class="lbl">Max accel (<span class="o-u">cm</span>/s²)</label>';
        h += '    <input type="number" id="oMaxAccel" step="any" min="0.1" max="' + stepsToDisp(_motorLimits.maxAccelSteps) + '" value="' + maxAccelDisp + '"></div>';
        h += '</div>';
        h += '<div class="field"><label class="lbl">Jog speed (<span class="o-u">cm</span>/s)</label>';
        h += '  <input type="number" id="oJogSpeed" step="any" min="0.1" max="' + stepsToDisp(_motorLimits.maxJogSteps) + '" value="' + jogSpeedDisp + '"></div>';
        h += '<p class="field-note">Motor currents are in the <b>Setup &amp; mechanical</b> card; cm/inch units in the control panel at the top.</p>';
        h += cardClose();

        // ── 3. Homing ────────────────────────────────────────────────────────
        // Everything that establishes or re-establishes the position=0
        // reference: direction, live SG readout for tuning, manual set-home,
        // auto-recovery mechanisms. All rehoming policies live here.
        h += cardOpen('Homing & recovery', 'oSumHome');
        h += '<span class="lbl">Manual mode</span>';
        h += '<div class="tog-row" style="margin-top:4px;background:rgba(198,124,0,.08);padding:8px;border-radius:6px;border:1px solid rgba(198,124,0,.35)">';
        h += '  <input type="checkbox" id="oManualToggle" onchange="orionSetManualMode(this.checked)">';
        h += '  <span class="tog-lbl"><b>Manual mode</b> — bypass stall detection, DMX watchdog and auto-rehome. Only manual jog + set-home; you are responsible for stopping the motor on a jam. The Home button becomes <b>Set home</b>.</span>';
        h += '</div>';
        h += '<div class="div" style="margin:8px 0"></div>';
        h += '<span class="lbl">Auto-home at boot</span>';
        h += '<p class="field-note">Independent from manual mode — you can enable both. On direct-drive rigs the load drops when the driver powers off; if enabled, the fixture treats the shaft position shortly after power-up as home.</p>';
        h += '<div class="tog-row" style="margin-top:4px">';
        h += '  <input type="checkbox" id="oHomeAtBoot"' + (_fix.homeAtBoot ? ' checked' : '') + '>';
        h += '  <span class="tog-lbl">Auto-set home at boot</span>';
        h += '</div>';
        h += '<div class="div" style="margin:8px 0"></div>';
        h += '<span class="lbl">Auto-recovery on stall</span>';
        h += '<div class="tog-row" style="margin-top:4px">';
        h += '  <input type="checkbox" id="oKeepHome"' + (fix.keepHomeOnStall ? ' checked' : '') + ' onchange="orionRecoveryGate()">';
        h += '  <span class="tog-lbl">Keep home after stall (worm-gear / self-locking drives)</span>';
        h += '</div>';
        h += '<p class="field-note">A stalled worm gear cannot back-drive, so the position reference physically survives. Leave OFF on direct-drive rigs — there a stall means lost steps and the home must be re-established. Excludes the auto-recovery options below.</p>';
        h += '<div class="tog-row" style="margin-top:6px">';
        h += '  <input type="checkbox" id="oAutoRehome"' + (autoRehomeOnStall ? ' checked' : '') + ' onchange="orionRecoveryGate()">';
        h += '  <span class="tog-lbl">Auto-rehome after Clear Fault</span>';
        h += '</div>';
        h += '<p class="field-note">When on, pressing Clear Fault (UI or DMX function byte 100-149) automatically starts homing. <b>⚠ Only enable if unattended homing motion is safe in your rig.</b></p>';
        h += '<div class="tog-row" style="margin-top:6px">';
        h += '  <input type="checkbox" id="oDropRehome"' + (dropReh ? ' checked' : '') + ' onchange="orionRecoveryGate()">';
        h += '  <span class="tog-lbl">Drop &amp; re-home (direct-drive)</span>';
        h += '</div>';
        h += '<div class="field" style="margin-top:6px"><label class="lbl">Drop wait (ms)</label>';
        h += '  <input type="number" id="oDropWait" min="100" max="10000" step="100" value="' + dropWait + '"></div>';
        h += '<p class="field-note">On direct-drive rigs, after Clear Fault the driver de-energises for the wait time so the load falls to a known resting point, then re-enables and sets home. Requires <b>Auto-rehome</b> to be on.</p>';
        h += cardClose();

        // ── Stall detection ──────────────────────────────────────────────────
        // StallGuard tuning: SGTHRS (auto-derived by Profile sweep but kept
        // editable for manual override), the Profile sweep entry, the σ
        // confidence selector, and Manual mode — which is fundamentally a
        // "disable stall detection" switch, so it lives here.
        h += cardOpen('Stall detection', 'oSumStall');
        // SG live readout — moved here from Homing since this is the card
        // where the operator actually decides what SGTHRS to set. Poll the
        // /sglive endpoint at 250 ms while any element with id oSgLive is
        // in the DOM (see the standalone poller at the bottom of the file).
        h += '<span class="lbl">StallGuard live</span>';
        h += '<div style="display:flex;gap:12px;align-items:center;font-size:12px;color:var(--txt2);margin:4px 0 8px">';
        h += '  <span>SG live: <b id="oSgLive" style="color:var(--txt);font-size:18px">--</b></span>';
        h += '  <span>state: <b id="oSgLiveState" style="color:var(--txt3)">--</b></span>';
        h += '</div>';
        h += '<p class="field-note">Jog the motor briefly (Jog &amp; Travel card) and watch the value. Operational SGTHRS should sit ~15–20 below the free-run reading.</p>';
        h += '<div class="div" style="margin:8px 0"></div>';
        h += '<div class="field"><label class="lbl">Operational SGTHRS</label>';
        h += '  <input type="number" id="oSgthrs" min="0" max="255" value="' + sgthrs + '"></div>';
        h += '<p class="field-note">Trip threshold used by both sensorless homing and operational stall detection. <b>Auto-derived by Profile sweep</b> — editable here only if you need to override the value manually.</p>';
        h += '<div style="margin-top:6px">';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionSgProfileOpen()">▶ Profile sweep</button>';
        h += '</div>';
        h += '<p class="field-note">The sweep captures SG_RESULT across 8 speed bins × 2 directions, then derives SGTHRS from the data. Re-run after any mechanical change (load, gear, drum).</p>';
        h += '<div class="div" style="margin:8px 0"></div>';
        h += '<div class="field"><label class="lbl">Stall sensitivity (sweep confidence)</label>';
        h += '  <select id="oSgSigma">';
        h += opt('2', 'Aggressive (2σ)',        sgSigma === 2);
        h += opt('3', 'Balanced (3σ, default)', sgSigma === 3);
        h += opt('4', 'Tolerant (4σ)',          sgSigma === 4);
        h += opt('5', 'Very tolerant (5σ)',     sgSigma === 5);
        h += '  </select></div>';
        h += '<p class="field-note">Applied at the next Profile sweep — how many σ below the mean the trip line sits. Higher = fewer false trips, catches real stalls slower. Small motors / direct-drive rigs typically need 4σ or 5σ.</p>';
        h += '<p class="field-note">Manual mode (disables stall detection) is in the <b>Homing &amp; recovery</b> card.</p>';
        h += cardClose();

        // ── 7. Setup & mechanical ────────────────────────────────────────────
        // Rare-touch commissioning card: mechanical calibration + motor
        // currents (electrical, ⚠). The guided wizard was removed — the cards
        // + the always-visible winch hub cover the whole setup flow directly.
        h += cardOpen('Setup & mechanical', 'oSumSetup');
        h += '<span class="lbl">Mechanical calibration</span>';
        h += '<div class="g2" style="margin-top:4px">';
        h += '  <div class="field"><label class="lbl">Drum diameter (mm)</label>';
        h += '    <input type="number" id="oDrum" min="1" value="' + drumDiameter + '"></div>';
        h += '  <div class="field"><label class="lbl">Steps per revolution</label>';
        h += '    <input type="number" id="oStepsRev" min="1" value="' + stepsPerRev + '"></div>';
        h += '</div>';
        h += '<div class="field"><label class="lbl">Gear ratio</label>';
        h += '  <input type="number" id="oGear" min="1" step="0.01" value="' + gearRatio + '"></div>';
        h += '<p class="field-note">Drum + gear + steps/rev convert cm values into stepper steps. Re-key these whenever you change the mechanical setup.</p>';
        h += '<div class="div" style="margin:10px 0"></div>';
        h += '<span class="lbl">Motor currents <span style="color:#ff9c3d">&#9888;</span></span>';
        h += '<div class="g2" style="margin-top:4px">';
        h += '  <div class="field"><label class="lbl">Run current (mA)</label>';
        h += '    <input type="number" id="oRunCurrent" min="100" max="2000" step="10" value="' + runCurrentMa + '"></div>';
        h += '  <div class="field"><label class="lbl">Hold current (mA)</label>';
        h += '    <input type="number" id="oHoldCurrent" min="0" max="1000" step="10" value="' + holdCurrentMa + '"></div>';
        h += '</div>';
        h += '<p class="field-note"><b style="color:#ff9c3d">&#9888; Electrical parameters</b> — wrong values can damage the driver or overheat the coil. Match your motor spec sheet. Applied on save.</p>';
        h += cardClose();

        h += '</div>';  // /.ch-list (motor sub-cards)
        h += '</div></div></div>';

        document.getElementById('fixtureSection').innerHTML = h;
        // Apply recovery-option dependency state to the freshly-rendered toggles.
        if (window.orionRecoveryGate) window.orionRecoveryGate();
        // Initial positive-range visualisation (updated live by pollStatus).
        if (window.updateTravelViz) window.updateTravelViz();
        if (window.updateHomeBtn) window.updateHomeBtn(!!_fix.manualMode);

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
    // Auto-fill Control start from Position start + the personality's position
    // block width (8-bit = 1 ch, 16-bit = 2 ch), so Control lands right after
    // Position with no gap — mirrors Veyron's veyronUpdateAddress(). Fired on
    // Position-start input and personality change (not on load, so a saved
    // custom Control start is preserved).
    window.orionAutoAddr = function () {
        const p  = parseInt(getV('oPersonality'))   || 1;
        const ps = parseInt(getV('oPositionStart')) || 1;
        const gap = (p === 1) ? 1 : 2;   // position channels before control block
        const csEl = document.getElementById('oControlStart');
        if (csEl) csEl.value = ps + gap;
        orionChMap();
    };

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
            ['oJogSpeed', 'oMaxSpeed', 'oMaxAccel', 'oTravel'].forEach(id => {
                const el = document.getElementById(id);
                if (!el || el.value === '') return;
                const v = parseFloat(el.value);
                if (!isNaN(v)) el.value = fmt(v * factor);
            });
        }
        updateTravelViz();
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
        // The Jog card's factor selector scales the configured speed for
        // fine positioning without touching the persisted value.
        let speed = cmToSteps(displayToCm(getV('oJogSpeed'))) || 1000;
        const fac = parseFloat(getV('oJogFactor')) || 1;
        speed = Math.max(100, Math.round(speed * fac));
        // Clamp to the backend jog cap and tell the operator — the input's
        // max attribute warns but doesn't stop an over-limit value from
        // being submitted, and an uncapped speed just loses steps.
        if (speed > _motorLimits.maxJogSteps) {
            speed = _motorLimits.maxJogSteps;
            showToast('Jog speed capped to ' + (_unit === 'in'
                ? (stepsToCm(speed) / 2.54).toFixed(1) + ' in/s'
                : stepsToCm(speed).toFixed(1) + ' cm/s'));
        }
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

    // Recovery-option dependency gating:
    //   keepHomeOnStall ON  → auto-rehome and drop&rehome disabled (nothing
    //                          to re-home, the reference survived the stall)
    //   autoRehome OFF      → drop&rehome disabled (drop is a variant of
    //                          the auto-rehome recovery flow)
    window.orionRecoveryGate = function () {
        const keep = document.getElementById('oKeepHome');
        const auto = document.getElementById('oAutoRehome');
        const drop = document.getElementById('oDropRehome');
        const wait = document.getElementById('oDropWait');
        if (!keep || !auto || !drop) return;
        const keepOn = keep.checked;
        auto.disabled = keepOn;
        if (keepOn) auto.checked = false;
        const autoOn = auto.checked && !keepOn;
        drop.disabled = !autoOn;
        if (!autoOn) drop.checked = false;
        if (wait) wait.disabled = !autoOn || !drop.checked;
    };

    // Manual mode toggle — persists to NVS on the server; the /motorstatus
    // badge + Home button gating update on the next poll.
    window.orionSetManualMode = function (on) {
        if (on && !confirm(
            'Enable Manual mode?\n\n' +
            'This disables StallGuard trip, DMX loss watchdog and auto-rehome. ' +
            'Only manual jog and Set-as-home will work. You take full responsibility ' +
            'for stopping the motor if something binds.\n\nProceed?'
        )) {
            const chk = document.getElementById('oManualToggle');
            if (chk) chk.checked = false;
            return;
        }
        const fd = new FormData();
        fd.append('on', on ? '1' : '0');
        fetch('/manualmode', {method: 'POST', body: fd}).then(r => {
            if (r.ok) showToast('Manual mode ' + (on ? 'ENABLED' : 'disabled'));
            else showToast('manualmode failed');
        }).catch(() => showToast('manualmode error'));
    };

    // SG live poller — separate 250 ms loop. It must only fire while the
    // operator is actually looking at the StallGuard readout: the element
    // lives in a card below the fold, so gating on mere DOM presence meant
    // 4 req/s forever, saturating the single-threaded AsyncTCP server and
    // starving the page load (fixture.js / images queued for seconds behind
    // the /sglive flood). Gate on tab-visible AND on-screen so a collapsed or
    // scrolled-away card produces zero traffic.
    setInterval(function () {
        if (document.visibilityState !== 'visible') return;
        const el = document.getElementById('oSgLive');
        if (!el) return;
        const r0 = el.getBoundingClientRect();
        const onScreen = r0.width > 0 && r0.height > 0 &&
                         r0.bottom > 0 && r0.top < (window.innerHeight || document.documentElement.clientHeight);
        if (!onScreen) return;
        fetch('/sglive').then(r => r.json()).then(s => {
            if (!s.available) return;
            const sg = document.getElementById('oSgLive');
            if (sg) sg.textContent = (s.sg == null) ? '--' : s.sg;
            const st = document.getElementById('oSgLiveState');
            if (st) st.textContent = s.state || '--';
        }).catch(() => {});
    }, 250);

    window.orionSetLimit = function (which) {
        const fd = new FormData();
        fd.append('which', which);
        fetch('/setlimit', {method: 'POST', body: fd}).then(r => {
            if (!r.ok) { showToast('setlimit failed'); return; }
            // Server returns the new limit in cm (one decimal). Push it into
            // the corresponding form input so the user sees what was saved,
            // converting to the currently selected display unit if needed.
            r.text().then(cm => {
                showToast(which.toUpperCase() + ' limit = ' + cm + ' cm');
                setTimeout(refreshTravelFromStatus, 300);
            });
        }).catch(() => showToast('setlimit error'));
    };

    // ── Positive-range travel helpers ────────────────────────────────────────
    // Home is always 0; Travel is the positive distance to the far end. The
    // far side is up when homing DOWN, down when homing UP (see the sign
    // convention in dmx_fixture.cpp: home-complete zeroes the homing side).
    window.orionSetFarHere = function () {
        const dir = parseInt(getV('oHomingDir')) || -1;
        orionSetLimit(dir < 0 ? 'up' : 'down');   // capture current pos as far
    };

    // Big Home button: manual mode declares the current position as home
    // (/sethome); otherwise runs sensorless homing (/home).
    window.orionHomeAction = function () {
        const b = document.getElementById('oHomeBig');
        if (b && b.classList.contains('set')) orionSetHome();
        else orionPost('/home');
    };
    // Reflect the running state on the big Home button: label follows manual
    // (Set home vs Run home), the blinking aura shows until the rig is homed.
    window.updateHomeBtn = function (manual, homed) {
        const b = document.getElementById('oHomeBig');
        if (!b) return;
        b.classList.toggle('set', !!manual);
        if (homed !== undefined) b.classList.toggle('homed', !!homed);
        b.innerHTML = manual ? '⌂ Set home' : '⌂ Run home';
    };
    // Jog speed factor buttons (Full / Half / ¼) → hidden #oJogFactor read by orionJog.
    window.orionSetJogFactor = function (v, btn) {
        const f = document.getElementById('oJogFactor'); if (f) f.value = v;
        if (btn) { [...btn.parentNode.children].forEach(x => x.classList.remove('on')); btn.classList.add('on'); }
    };

    // Populate the Travel field from a default range; user reviews then Saves.
    // Save derives home-side = 0 + far = ±Travel, re-aligning a skewed config.
    window.orionResetTravel = function () {
        const el = document.getElementById('oTravel');
        if (el) el.value = (_unit === 'in' ? (200 / 2.54) : 200).toFixed(2);
        updateTravelViz();
        showToast('Travel set to 200 ' + _unit + ' — Save to apply (home = 0, far = 200)');
    };

    // Pull the current far limit from the device into the Travel field.
    function refreshTravelFromStatus() {
        fetch('/motorstatus').then(r => r.json()).then(s => {
            const dir  = parseInt(getV('oHomingDir')) || -1;
            const farCm = Math.abs(dir < 0 ? (s.upCm || 0) : (s.downCm || 0));
            const el = document.getElementById('oTravel');
            if (el) el.value = (_unit === 'in' ? (farCm / 2.54) : farCm).toFixed(2);
            updateTravelViz(s);
        }).catch(() => {});
    }

    // Drive the winch diagram (mockup layout): drum at the home end, cable to
    // the live load, home/far markers, homing arrow, DMX ticks. s (optional)
    // is a /motorstatus sample for the live load position.
    window.updateTravelViz = function (s) {
        const band = document.getElementById('ovBand');
        if (!band) return;
        // Track spans the full card height — derive TOP/BOT from the live size.
        const _wrap = band.parentElement;
        const _wh = (_wrap && _wrap.clientHeight) ? _wrap.clientHeight : 300;
        const TOP = 18, BOT = _wh - 18, H = BOT - TOP;
        const dir    = parseInt(getV('oHomingDir')) || -1;
        const inv    = !!(document.getElementById('oDmxInv') && document.getElementById('oDmxInv').checked);
        const travel = parseFloat(getV('oTravel')) || 0;      // display unit
        const u = _unit, homeUp = dir > 0;
        const homeY = homeUp ? TOP : BOT, farY = homeUp ? BOT : TOP;

        band.style.top = TOP + 'px'; band.style.height = H + 'px';
        band.style.background = homeUp ? 'linear-gradient(#3ddc84,#ff9c3d)'
                                       : 'linear-gradient(#ff9c3d,#3ddc84)';
        document.getElementById('ovDrum').style.top = (homeY - 15) + 'px';

        // Live load position along the range (green; orange if out of range).
        let f = 0, oob = false;
        if (s && s.positionCm != null) {
            const travelCm = displayToCm(String(travel)) || 0;
            f = travelCm > 0 ? Math.abs(s.positionCm) / travelCm : 0;
            oob = f > 1.001; f = Math.max(0, Math.min(1, f));
        }
        let loadY = homeY + (farY - homeY) * f;
        // Keep the load clear of the drum even at/near home (live pos = 0),
        // so the block never overlaps the drum circle at the home end.
        const sgn = (farY >= homeY) ? 1 : -1;
        if (Math.abs(loadY - homeY) < 24) loadY = homeY + sgn * 24;
        const load = document.getElementById('ovLoad');
        load.style.top = (loadY - 7) + 'px';
        load.className = 'ovload' + (oob ? ' oob' : '');
        const cable = document.getElementById('ovCable');
        cable.style.top = Math.min(homeY, loadY) + 'px';
        cable.style.height = Math.abs(loadY - homeY) + 'px';

        document.getElementById('ovHome').style.top = (homeY - 15) + 'px';
        document.getElementById('ovFar').style.top  = (farY - 15) + 'px';

        const arrow = document.getElementById('ovArrow');
        arrow.innerHTML = homeUp ? '&#9650;' : '&#9660;';
        arrow.style.top = (homeUp ? TOP + 2 : BOT - 16) + 'px';

        const dtop = document.getElementById('ovDmxTop'), dbot = document.getElementById('ovDmxBot');
        dtop.innerHTML = '<b>DMX ' + (inv ? '0' : '255') + '</b><s>top</s>';  dtop.style.top = (TOP - 4) + 'px';
        dbot.innerHTML = '<b>DMX ' + (inv ? '255' : '0') + '</b><s>floor</s>'; dbot.style.top = (BOT - 12) + 'px';

        const liveEl = document.getElementById('ovLive');
        if (liveEl) {
            if (s && s.positionCm != null) {
                const p = (u === 'in') ? (s.positionCm / 2.54) : s.positionCm;
                liveEl.innerHTML = '<b style="color:#3ddc84">live</b> ' + p.toFixed(1) + ' ' + u
                    + (oob ? ' <span style="color:#ff9c3d">&#9888; out of range</span>' : '');
            } else liveEl.textContent = '';
        }
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
            // Sweep phase enum (mirror of SgSweepPhase in dmx_fixture.cpp):
            //   0=IDLE, 1=PRE_POSITION, 2=RUN_BIN, 3=COMPLETE, 4=ABORTED
            // The previous mapping treated 2 as "done" and 3 as "aborted" —
            // one off after PRE_POSITION was introduced — so the modal said
            // "Profile saved" while bins were still running and "aborted"
            // when the sweep had actually completed.
            _swp.timer = setInterval(async () => {
                const p = await (await fetch('/sgsweep')).json();
                setText('oSwpBin', p.currentBin);
                setText('oSwpSg',  p.lastSg);
                const pct = (p.currentBin / p.totalBins) * 100;
                const fill = $('oSwpFill');
                if (fill) fill.style.width = pct + '%';
                if (p.phase === 1) {
                    setText('oSwpStatus', 'Pre-positioning to the opposite end…');
                    return;
                }
                // Warn once if the device had to cap the step to fit the travel.
                if (p.capped && !cappedNoted) {
                    cappedNoted = true;
                    const topBin = p.baseSpeed + 7 * p.speedStep;
                    setText('oSwpStatus', '⚠ Travel too short — top bin capped at ' + topBin + ' step/s (no stall coverage above)');
                } else if (p.phase === 2 && !p.capped) {
                    setText('oSwpStatus', 'Running ' + dir.toUpperCase() + ' sweep — bin ' + p.currentBin + '/8…');
                }
                if (p.phase === 3) {
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
                        setText('oSwpStatus', '✓ Both directions done — profile saved.' + cappedHint);
                        $('oSwpStartB').style.display = 'none';
                    }
                } else if (p.phase === 4) {
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
        // Positive-range model → raw down/upPosition. Home = 0 (the homing
        // side); far = +Travel on the opposite side. home DOWN (dir<0):
        // down=0, up=+Travel. home UP (dir>0): up=0, down=-Travel.
        const _dir        = parseInt(getV('oHomingDir')) || -1;
        const _travelStps = Math.max(0, toSteps('oTravel'));
        const _downPos    = _dir < 0 ? 0 : -_travelStps;
        const _upPos      = _dir < 0 ? _travelStps : 0;
        const out = {
            personality:       parseInt(getV('oPersonality'))   || 1,
            positionStart:     parseInt(getV('oPositionStart')) || 1,
            controlStart:      parseInt(getV('oControlStart'))  || 3,
            downPosition:      _downPos,
            upPosition:        _upPos,
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
            autoRehomeOnStall: !!(document.getElementById('oAutoRehome') && document.getElementById('oAutoRehome').checked),
            dmxWatchdogAction: parseInt(getV('oWdAction'))      || 0,
            operSgthrs:        parseInt(getV('oSgthrs'))        || 100,
            homeAtBoot:        !!(document.getElementById('oHomeAtBoot') && document.getElementById('oHomeAtBoot').checked),
            keepHomeOnStall:   !!(document.getElementById('oKeepHome') && document.getElementById('oKeepHome').checked),
            dropAndRehome:     !!(document.getElementById('oDropRehome') && document.getElementById('oDropRehome').checked),
            dropWaitMs:        parseInt(getV('oDropWait')) || 3000,
            sgConfidenceSigma: parseInt(getV('oSgSigma')) || 3,
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
