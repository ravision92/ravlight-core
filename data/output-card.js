// Shared LED-output card renderer. Used by fixture-elyon.js and the LED tab of
// fixture-orion.js. Encapsulates the per-output card HTML, the section-visibility
// rules in protoChange, the per-card summary display, and the read-back to JSON.
//
// Fixtures install a `window.fixtureRecalc` callback that the card invokes when
// the user edits pixel count / backend / protocol — that callback updates the
// fixture-level budget bar and DMX-layout summary.

window.outputCard = (function () {

    const P_WS2811   = 0;
    const P_WS2812B  = 1;
    const P_SK6812   = 2;
    const P_WS2814   = 3;
    const P_WS2815   = 4;
    const P_TM1814   = 5;
    const P_TM1914   = 6;
    const P_APA102   = 7;
    const P_SK9822   = 8;
    const P_P9813    = 9;
    const P_PWM      = 50;
    const P_RELAY    = 51;
    const P_FOLLOWER = 60;

    const PROTO_NAMES = {
        0: 'WS2811', 1: 'WS2812B', 2: 'SK6812 RGBW', 3: 'WS2814 RGBW', 4: 'WS2815',
        5: 'TM1814 RGBW', 6: 'TM1914 RGBW', 7: 'APA102', 8: 'SK9822', 9: 'P9813',
        50: 'PWM Dimmer', 51: 'Relay', 60: 'CLOCK follower',
    };

    function chPerPixel(proto) {
        if (proto === P_PWM || proto === P_RELAY || proto === P_FOLLOWER) return 1;
        return (proto === P_SK6812 || proto === P_WS2814 ||
                proto === P_TM1814 || proto === P_TM1914) ? 4 : 3;
    }
    function isClocked(p) { return p === P_APA102 || p === P_SK9822 || p === P_P9813; }
    function escapeAttr(s) { return String(s == null ? '' : s).replace(/"/g, '&quot;'); }

    // Build the card HTML for output index `i`. `o` is the per-output JSON object,
    // `F` is the SPA feature flags, `N` is the total output count (used by the
    // CLOCK partner selector).
    function render(o, i, F, N) {
        o = o || {};
        const proto    = (o.proto !== undefined) ? Number(o.proto) : P_WS2812B;
        const isPwm    = (proto === P_PWM);
        const isRelay  = (proto === P_RELAY);
        const isFollow = (proto === P_FOLLOWER);
        const isClk    = isClocked(proto);

        let h = '<div class="ch-card" id="card-' + i + '">';
        h += '<div class="ch-head" onclick="window.outputCard.toggle(' + i + ')">';
        h += '  <div class="ch-id">';
        h += '    <span class="ch-num">CH' + (i + 1) + '</span>';
        const gpio = (F && F.hw_pins && F.hw_pins[i] !== undefined) ? F.hw_pins[i] : -1;
        h += '    <span class="ch-gpio">GPIO ' + gpio + '</span>';
        h += '  </div>';
        h += '  <div class="ch-sum">';
        h += '    <span class="ch-proto" id="sProto' + i + '">' + (PROTO_NAMES[proto] || ('proto ' + proto)) + '</span>';
        h += '    <div class="ch-tags">';
        h += '      <span class="ch-tag" id="sP1' + i + '"></span>';
        h += '      <span class="ch-tag">U:<b id="sU' + i + '">' + (o.univ || 0) + '</b></span>';
        h += '      <span class="ch-tag">CH:<b id="sCH' + i + '">' + (o.ch || 1) + '</b></span>';
        h += '    </div>';
        h += '  </div>';
        h += '  <span class="ch-arr">&#9661;</span>';
        h += '</div>';
        h += '<div class="ch-body"><div class="ch-form">';

        if (isFollow) {
            h += '<p class="field-note">This output is used as the <b>CLOCK</b> line of another output. Configure or free it from there.</p>';
            h += '<input type="hidden" id="proto' + i + '" value="60">';
            h += '</div></div></div>';
            return h;
        }

        // Protocol
        h += '<div class="field"><label class="lbl">Protocol</label>';
        h += '<select id="proto' + i + '" onchange="window.outputCard.protoChange(' + i + ')">';
        [P_WS2811, P_WS2812B, P_SK6812, P_WS2814, P_WS2815, P_TM1814, P_TM1914,
         P_APA102, P_SK9822, P_P9813, P_PWM, P_RELAY].forEach(p => {
            h += '<option value="' + p + '"' + (p === proto ? ' selected' : '') + '>' +
                 PROTO_NAMES[p] + (isClocked(p) ? ' (clocked)' : '') + '</option>';
        });
        h += '</select></div>';

        // Backend (only on I2S builds)
        if (F && F.i2s) {
            h += '<div class="field" id="backendSec' + i + '">';
            h += '<label class="lbl">Driver backend</label>';
            h += '<select id="backend' + i + '" onchange="if(window.fixtureRecalc)window.fixtureRecalc()">';
            const b = (o.be !== undefined) ? Number(o.be) : 1;
            h += '<option value="0"' + (b === 0 ? ' selected' : '') + '>RMT (1 channel per output, max 8)</option>';
            h += '<option value="1"' + (b === 1 ? ' selected' : '') + '>I2S (shared parallel, max 8)</option>';
            h += '</select>';
            h += '<p class="field-note">Changing the backend requires a device restart.</p>';
            h += '</div>';
        }

        // Relay
        h += '<div id="relaySec' + i + '">';
        h += '<div class="field"><label class="lbl">ON threshold (0–255)</label>';
        h += '<input type="number" id="rthr' + i + '" value="' + (o.relay_thr || 128) + '" min="0" max="255"></div>';
        h += '<div class="tog-row" style="margin-top:6px">';
        h += '<input type="checkbox" id="rinv' + i + '"' + (o.relay_inv ? ' checked' : '') + '>';
        h += '<span class="tog-lbl">Active-low (invert output)</span></div></div>';

        // Clocked partner
        h += '<div id="clockSec' + i + '">';
        h += '<div class="field"><label class="lbl">CLOCK partner output</label>';
        h += '<select id="clkp' + i + '">';
        for (let j = 0; j < N; j++) {
            if (j === i) continue;
            h += '<option value="' + j + '"' + (o.clock_p === j ? ' selected' : '') + '>CH' + (j + 1) + '</option>';
        }
        h += '</select></div>';
        h += '<p class="field-note">The chosen output is reserved as CLOCK line.</p>';
        h += '</div>';

        // PWM
        h += '<div id="pwmSec' + i + '">';
        h += '<div class="field"><label class="lbl">PWM Frequency</label>';
        h += '<select id="freq' + i + '" onchange="if(window.fixtureRecalc)window.fixtureRecalc()">';
        [100, 500, 1000, 5000, 10000, 20000].forEach(f => {
            h += '<option value="' + f + '"' + ((o.pwm_freq || 1000) === f ? ' selected' : '') +
                 '>' + (f < 1000 ? f + ' Hz' : (f / 1000) + ' kHz') + '</option>';
        });
        h += '</select></div>';
        h += '<div class="g2" style="margin-top:8px">';
        h += '<div class="field"><label class="lbl">Curve</label>';
        h += '<select id="curve' + i + '">';
        h += '<option value="0"' + (!o.pwm_curve ? ' selected' : '') + '>Linear</option>';
        h += '<option value="1"' + (o.pwm_curve === 1 ? ' selected' : '') + '>Quadratic γ2</option>';
        h += '<option value="2"' + (o.pwm_curve === 2 ? ' selected' : '') + '>Cubic γ3</option>';
        h += '</select></div>';
        h += '<div class="tog-row"><input type="checkbox" id="bit16' + i + '"' + (o.pwm_16bit ? ' checked' : '') + '><span class="tog-lbl">16-bit (2 DMX ch)</span></div>';
        h += '</div>';
        h += '<div class="tog-row" style="margin-top:6px"><input type="checkbox" id="pinv' + i + '"' + (o.pwm_inv ? ' checked' : '') + '><span class="tog-lbl">Invert PWM duty</span></div>';
        h += '</div>';

        // Pixel
        h += '<div id="pxSec' + i + '">';
        h += '<div class="g2">';
        h += '<div class="field"><label class="lbl">Pixel count</label>';
        h += '<input type="number" id="count' + i + '" value="' + (o.count || 0) + '" min="0" max="1024" oninput="if(window.fixtureRecalc)window.fixtureRecalc()"></div>';
        h += '<div class="field"><label class="lbl">Grouping</label>';
        h += '<input type="number" id="grp' + i + '" value="' + (o.group || 1) + '" min="1" max="32" oninput="if(window.fixtureRecalc)window.fixtureRecalc()"></div>';
        h += '</div>';
        h += '<div class="g2" style="margin-top:8px">';
        h += '<div class="field"><label class="lbl">Color order</label>';
        h += '<input type="text" id="order' + i + '" value="' + escapeAttr(o.order || 'RGB') + '" maxlength="4" pattern="[RGBWrgbw]+"></div>';
        h += '<div class="tog-row"><input type="checkbox" id="inv' + i + '"' + (o.inv ? ' checked' : '') + '><span class="tog-lbl">Invert chain</span></div>';
        h += '</div>';
        h += '</div>';

        // Brightness
        h += '<div id="briSec' + i + '" class="field"><label class="lbl">Brightness</label>';
        h += '<input type="number" id="bri' + i + '" value="' + (o.bri == null ? 255 : o.bri) + '" min="0" max="255"></div>';

        // Universe + channel
        h += '<div class="g2">';
        h += '<div class="field"><label class="lbl">Universe</label>';
        h += '<input type="number" id="univ' + i + '" value="' + (o.univ || 0) + '" min="0" max="32768" oninput="if(window.fixtureRecalc)window.fixtureRecalc()"></div>';
        h += '<div class="field"><label class="lbl">DMX start ch</label>';
        h += '<input type="number" id="sch' + i + '" value="' + (o.ch || 1) + '" min="1" max="512" oninput="if(window.fixtureRecalc)window.fixtureRecalc()"></div>';
        h += '</div>';

        // Highlight
        h += '<button type="button" class="act-btn" style="margin-top:10px" onclick="window.outputCard.highlight(' + i + ')">Highlight (white wipe)</button>';

        h += '</div></div></div>';
        return h;
    }

    function toggle(i) {
        const c = document.getElementById('card-' + i);
        if (!c) return;
        const body = c.querySelector('.ch-body');
        if (!body) return;
        const opening = !c.classList.contains('open');
        c.classList.toggle('open');
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
    }

    function protoChange(i) {
        const p = parseInt(document.getElementById('proto' + i).value);
        const isPwm   = (p === P_PWM);
        const isRelay = (p === P_RELAY);
        const isClk   = isClocked(p);
        const isPx    = !isPwm && !isRelay;
        const show = (id, cond) => { const el = document.getElementById(id + i); if (el) el.style.display = cond ? '' : 'none'; };
        show('pwmSec',     isPwm);
        show('pxSec',      isPx);
        show('relaySec',   isRelay);
        show('briSec',     !isRelay);
        show('clockSec',   isClk);
        show('backendSec', isPx && !isClk);
        sumUpdate(i);
        if (window.fixtureRecalc) window.fixtureRecalc();
    }

    function sumUpdate(i) {
        const p = parseInt((document.getElementById('proto' + i) || {value: '1'}).value);
        const sp = document.getElementById('sProto' + i);
        if (sp) sp.textContent = PROTO_NAMES[p] || ('proto ' + p);
        const s1 = document.getElementById('sP1' + i);
        if (s1) {
            if (p === P_PWM)        s1.innerHTML = '<b>' + (document.getElementById('freq' + i) || {value: '1000'}).value + 'Hz</b>';
            else if (p === P_RELAY) s1.innerHTML = 'thr:<b>' + (document.getElementById('rthr' + i) || {value: '128'}).value + '</b>';
            else                    s1.innerHTML = '<b>' + (document.getElementById('count' + i) || {value: '0'}).value + '</b> px';
        }
        const su = document.getElementById('sU' + i);
        const sc = document.getElementById('sCH' + i);
        if (su) su.textContent = (document.getElementById('univ' + i) || {value: '0'}).value;
        if (sc) sc.textContent = (document.getElementById('sch'  + i) || {value: '1'}).value;
    }

    function highlight(i) {
        const fd = new FormData(); fd.append('out', i);
        fetch('/ledhighlight', {method: 'POST', body: fd}).catch(() => {});
    }

    function getV(id)   { const el = document.getElementById(id); return el ? el.value : ''; }
    function getChk(id) { const el = document.getElementById(id); return el ? el.checked : false; }

    // Serialise one card back into the JSON shape the firmware expects.
    function read(i, F) {
        const protoEl = document.getElementById('proto' + i);
        if (!protoEl) return {proto: 1, count: 0, univ: 0, ch: 1};
        const p = parseInt(protoEl.value);
        const o = {
            proto: p,
            univ:  parseInt(getV('univ' + i)) || 0,
            ch:    parseInt(getV('sch'  + i)) || 1,
            inv:   getChk('inv'  + i) ? 1 : 0,
        };
        if (p === P_RELAY) {
            o.relay_thr = parseInt(getV('rthr' + i)) || 128;
            o.relay_inv = getChk('rinv' + i) ? 1 : 0;
        } else if (p === P_PWM) {
            o.pwm_freq  = parseInt(getV('freq' + i)) || 0;
            o.pwm_curve = parseInt(getV('curve' + i)) || 0;
            o.pwm_16bit = getChk('bit16' + i) ? 1 : 0;
            o.pwm_inv   = getChk('pinv' + i)  ? 1 : 0;
            o.bri       = parseInt(getV('bri' + i)) || 255;
        } else if (p === P_FOLLOWER) {
            // No further fields.
        } else {
            o.count = parseInt(getV('count' + i)) || 0;
            o.group = parseInt(getV('grp' + i)) || 1;
            o.bri   = parseInt(getV('bri' + i)) || 255;
            o.order = (getV('order' + i) || '').toUpperCase();
            if (isClocked(p)) {
                o.clock_p = parseInt(getV('clkp' + i)) || 0;
            } else if (F && F.i2s) {
                const b = parseInt(getV('backend' + i));
                if (!isNaN(b)) o.be = b;
            }
        }
        return o;
    }

    return {
        // Constants exposed for fixture-level budget math.
        P_WS2811, P_WS2812B, P_SK6812, P_WS2814, P_WS2815, P_TM1814, P_TM1914,
        P_APA102, P_SK9822, P_P9813, P_PWM, P_RELAY, P_FOLLOWER,
        chPerPixel, isClocked,
        render, read, protoChange, toggle, sumUpdate, highlight,
    };
})();
