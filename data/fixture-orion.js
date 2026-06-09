// Orion fixture renderer — winch motor (TMC2209) + optional LED outputs.
// Functional first pass: save-relevant fields + action buttons. Live status
// polling, StallGuard calibration wizard, and per-output LED card UI are
// follow-up work — for now the LED outputs are rendered as a read-only summary.

(function () {
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

    window.renderFixture = function (fix /*, features */) {
        _fix = fix || {};
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
        h += '  <span>pos: <b id="orionPosCm" style="color:var(--txt)">--</b> steps</span>';
        h += '  <span>sg: <b id="orionSg" style="color:var(--txt)">--</b></span>';
        h += '  <span>temp: <b id="orionTemp" style="color:var(--txt)">--</b>&deg;C</span>';
        h += '  <span id="orionHomed" style="color:var(--txt3)">not homed</span>';
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
        h += '<p class="field-note">Homing detects the end stop via StallGuard. The operating threshold is tuned with the calibration wizard (TODO).</p>';
        h += '<div class="field"><label class="lbl">StallGuard threshold (SGTHRS)</label>';
        h += '  <input type="number" id="oSgthrs" min="0" max="255" value="' + sgthrs + '"></div>';
        h += cardClose();

        // ── 3. Manual jog ────────────────────────────────────────────────────
        h += cardOpen('Manual jog (calibration)', 'oSumJog');
        h += '<div class="field"><label class="lbl">Jog speed (steps/s)</label>';
        h += '  <input type="number" id="oJogSpeed" min="1" value="' + jogSpeed + '"></div>';
        h += '<div style="display:flex;gap:6px;margin-top:6px">';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onmousedown="orionJog(1)" onmouseup="orionJogStop()" ontouchstart="orionJog(1)" ontouchend="orionJogStop()">&#x25B2; Jog Up</button>';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onmousedown="orionJog(-1)" onmouseup="orionJogStop()" ontouchstart="orionJog(-1)" ontouchend="orionJogStop()">&#x25BC; Jog Down</button>';
        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="orionPost(\'/release-dmx\')">Release to DMX</button>';
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

        h += '</div>';  // /.ch-list
        h += '</div></div></div>';

        document.getElementById('fixtureSection').innerHTML = h;
    };

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

    // ── Serialise → /api/config ──────────────────────────────────────────────

    window.getFixtureData = function () {
        return {
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
    };

    function getV(id) { const el = document.getElementById(id); return el ? el.value : ''; }
})();
