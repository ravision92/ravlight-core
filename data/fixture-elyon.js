// Elyon fixture renderer — builds the per-output cards client-side from
// /api/config and exposes window.getFixtureData() so the SPA shell can collect
// the user's input for save.

(function () {
    // Protocol enum mirrors include/core/output_config.h
    const P_WS2811     = 0;
    const P_WS2812B    = 1;
    const P_SK6812     = 2;
    const P_WS2814     = 3;
    const P_WS2815     = 4;
    const P_TM1814     = 5;
    const P_TM1914     = 6;
    const P_APA102     = 7;
    const P_SK9822     = 8;
    const P_P9813      = 9;
    const P_PWM        = 50;
    const P_RELAY      = 51;
    const P_FOLLOWER   = 60;

    const PROTO_NAMES = {
        0: 'WS2811', 1: 'WS2812B', 2: 'SK6812 RGBW', 3: 'WS2814 RGBW', 4: 'WS2815',
        5: 'TM1814 RGBW', 6: 'TM1914 RGBW', 7: 'APA102', 8: 'SK9822', 9: 'P9813',
        50: 'PWM Dimmer', 51: 'Relay', 60: 'CLOCK follower'
    };

    let N = 0;     // HW_LED_OUTPUT_COUNT (from /api/features)
    let F = {};

    function chPerPixel(proto) {
        if (proto === P_PWM || proto === P_RELAY || proto === P_FOLLOWER) return 1;
        return (proto === P_SK6812 || proto === P_WS2814 ||
                proto === P_TM1814 || proto === P_TM1914) ? 4 : 3;
    }
    function isClocked(p) { return p === P_APA102 || p === P_SK9822 || p === P_P9813; }
    function isPixel(p)   { return p < 50; }

    function escapeAttr(s) { return String(s == null ? '' : s).replace(/"/g, '&quot;'); }

    // ── Render ────────────────────────────────────────────────────────────────

    window.renderFixture = function (fix, features) {
        F = features;
        N = features.hw_outputs || 8;
        const outputs = fix.outputs || [];
        // Pad missing slots with defaults so we always render N cards.
        while (outputs.length < N) {
            outputs.push({proto: 1, count: 0, univ: 0, ch: 1, group: 1, inv: 0, bri: 255, order: 'RGB'});
        }

        // Same wrapper structure the C++ ELYON_FIXTURE_HTML used to emit. The
        // accordion button is display:none inside .tabsec, but the wrapper styling
        // (border, rounded corners, internal padding) still applies.
        let html = '';
        html += '<div class="acc-wrap">';
        html += '  <button type="button" class="acc-btn open">';
        html += '    <span>' + (features.fixture || 'Fixture') + ' Outputs ';
        html += '      <span style="font-size:11px;color:var(--txt3);font-weight:400">&middot; ' + N + ' CH</span>';
        html += '    </span>';
        html += '    <span class="acc-arrow">&#9661;</span>';
        html += '  </button>';
        html += '  <div class="acc-body open">';
        html += '    <div style="padding:12px 12px 4px">';
        html += '      <div class="tog-row">';
        html += '        <input type="checkbox" id="elyonAutoLayout">';
        html += '        <span class="tog-lbl">Auto-calculate universes &amp; channels</span>';
        html += '      </div>';
        html += '    </div>';
        html += '    <div class="ch-list" id="elyonCards"></div>';
        html += '    <div class="budget">';
        html += '      <span class="bud-lbl">Pixels</span>';
        html += '      <div class="bud-bar"><div class="bud-fill" id="budFill"></div></div>';
        html += '      <span class="bud-val" id="budVal">0 / 0</span>';
        html += '    </div>';
        html += '    <div class="timer-warn" id="timerWarn"></div>';
        html += '    <div style="height:10px"></div>';
        html += '  </div>';
        html += '</div>';

        const root = document.getElementById('fixtureSection');
        root.innerHTML = html;

        const cards = document.getElementById('elyonCards');
        for (let i = 0; i < N; i++) cards.insertAdjacentHTML('beforeend', renderCard(outputs[i] || {}, i));

        // Initial pass to sync visibility per protocol / backend.
        for (let i = 0; i < N; i++) window.elyonProtoChange(i);
        window.elyonRecalc();
        autoLayoutChanged();
        document.getElementById('elyonAutoLayout').addEventListener('change', autoLayoutChanged);
    };

    function renderCard(o, i) {
        const proto    = (o.proto !== undefined) ? Number(o.proto) : P_WS2812B;
        const isPwm    = (proto === P_PWM);
        const isRelay  = (proto === P_RELAY);
        const isFollow = (proto === P_FOLLOWER);
        const isClk    = isClocked(proto);
        const isPx     = isPixel(proto) && !isClk;

        // Owner of this slot when in FOLLOWER state (which clocked sibling consumes us)
        let followerOwner = -1;
        // (Fully computed from server-side data — we have it via o, but cross-output
        // logic is done in protoChange/recalc; for the initial render we just label
        // the card as follower if proto is FOLLOWER.)

        let h = '<div class="ch-card" id="card-'+i+'">';
        h += '<div class="ch-head" onclick="elyonToggleCh('+i+')">';
        h += '  <div class="ch-id">';
        h += '    <span class="ch-num">CH'+(i+1)+'</span>';
        const gpio = (F.hw_pins && F.hw_pins[i] !== undefined) ? F.hw_pins[i] : -1;
        h += '    <span class="ch-gpio" id="sGpio'+i+'">GPIO '+gpio+'</span>';
        h += '  </div>';
        h += '  <div class="ch-sum">';
        h += '    <span class="ch-proto" id="sProto'+i+'">'+(PROTO_NAMES[proto] || ('proto '+proto))+'</span>';
        h += '    <div class="ch-tags">';
        h += '      <span class="ch-tag" id="sP1'+i+'"></span>';
        h += '      <span class="ch-tag">U:<b id="sU'+i+'">'+(o.univ || 0)+'</b></span>';
        h += '      <span class="ch-tag">CH:<b id="sCH'+i+'">'+(o.ch || 1)+'</b></span>';
        h += '    </div>';
        h += '  </div>';
        h += '  <span class="ch-arr">&#9661;</span>';
        h += '</div>';
        h += '<div class="ch-body"><div class="ch-form">';

        if (isFollow) {
            h += '<p class="field-note">This output is used as the <b>CLOCK</b> line of another output. Configure or free it from there.</p>';
            h += '<input type="hidden" id="proto'+i+'" value="60">';
            h += '</div></div></div>';
            return h;
        }

        // Protocol
        h += '<div class="field"><label class="lbl">Protocol</label>';
        h += '<select id="proto'+i+'" onchange="elyonProtoChange('+i+')">';
        [P_WS2811,P_WS2812B,P_SK6812,P_WS2814,P_WS2815,P_TM1814,P_TM1914,P_APA102,P_SK9822,P_P9813,P_PWM,P_RELAY].forEach(p => {
            h += '<option value="'+p+'"'+(p===proto?' selected':'')+'>'+PROTO_NAMES[p]+(isClocked(p)?' (clocked)':'')+'</option>';
        });
        h += '</select></div>';

        // Backend (data-mod=i2s removes it on RMT-only builds)
        if (F.i2s) {
            h += '<div class="field" id="backendSec'+i+'">';
            h += '<label class="lbl">Driver backend</label>';
            h += '<select id="backend'+i+'" onchange="elyonRecalc()">';
            const b = (o.be !== undefined) ? Number(o.be) : 1; // I2S default on I2S build
            h += '<option value="0"'+(b===0?' selected':'')+'>RMT (1 channel per output, max 8)</option>';
            h += '<option value="1"'+(b===1?' selected':'')+'>I2S (shared parallel, max 8)</option>';
            h += '</select>';
            h += '<p class="field-note">Changing the backend requires a device restart.</p>';
            h += '</div>';
        }

        // Relay section
        h += '<div id="relaySec'+i+'">';
        h += '<div class="field"><label class="lbl">ON threshold (0–255)</label>';
        h += '<input type="number" id="rthr'+i+'" value="'+(o.relay_thr || 128)+'" min="0" max="255"></div>';
        h += '<div class="tog-row" style="margin-top:6px">';
        h += '<input type="checkbox" id="rinv'+i+'"'+(o.relay_inv?' checked':'')+'>';
        h += '<span class="tog-lbl">Active-low (invert output)</span></div></div>';

        // Clocked partner selector
        h += '<div id="clockSec'+i+'">';
        h += '<div class="field"><label class="lbl">CLOCK partner output</label>';
        h += '<select id="clkp'+i+'">';
        for (let j = 0; j < N; j++) {
            if (j === i) continue;
            h += '<option value="'+j+'"'+(o.clock_p === j ? ' selected' : '')+'>CH'+(j+1)+'</option>';
        }
        h += '</select></div>';
        h += '<p class="field-note">The chosen output is reserved as CLOCK line.</p>';
        h += '</div>';

        // PWM section
        h += '<div id="pwmSec'+i+'">';
        h += '<div class="field"><label class="lbl">PWM Frequency</label>';
        h += '<select id="freq'+i+'" onchange="elyonRecalc()">';
        [100,500,1000,5000,10000,20000].forEach(f => {
            h += '<option value="'+f+'"'+((o.pwm_freq||1000)===f?' selected':'')+'>'+(f<1000?f+' Hz':(f/1000)+' kHz')+'</option>';
        });
        h += '</select></div>';
        h += '<div class="g2" style="margin-top:8px">';
        h += '<div class="field"><label class="lbl">Curve</label>';
        h += '<select id="curve'+i+'"><option value="0"'+(!o.pwm_curve?' selected':'')+'>Linear</option><option value="1"'+(o.pwm_curve===1?' selected':'')+'>Quadratic γ2</option><option value="2"'+(o.pwm_curve===2?' selected':'')+'>Cubic γ3</option></select></div>';
        h += '<div class="tog-row"><input type="checkbox" id="bit16'+i+'"'+(o.pwm_16bit?' checked':'')+'><span class="tog-lbl">16-bit (2 DMX ch)</span></div>';
        h += '</div>';
        h += '<div class="tog-row" style="margin-top:6px"><input type="checkbox" id="pinv'+i+'"'+(o.pwm_inv?' checked':'')+'><span class="tog-lbl">Invert PWM duty</span></div>';
        h += '</div>';

        // Pixel section (count, group, order, invert)
        h += '<div id="pxSec'+i+'">';
        h += '<div class="g2">';
        h += '<div class="field"><label class="lbl">Pixel count</label>';
        h += '<input type="number" id="count'+i+'" value="'+(o.count||0)+'" min="0" max="1024" oninput="elyonRecalc()"></div>';
        h += '<div class="field"><label class="lbl">Grouping</label>';
        h += '<input type="number" id="grp'+i+'" value="'+(o.group||1)+'" min="1" max="32" oninput="elyonRecalc()"></div>';
        h += '</div>';
        h += '<div class="g2" style="margin-top:8px">';
        h += '<div class="field"><label class="lbl">Color order</label>';
        h += '<input type="text" id="order'+i+'" value="'+escapeAttr(o.order||'RGB')+'" maxlength="4" pattern="[RGBWrgbw]+"></div>';
        h += '<div class="tog-row"><input type="checkbox" id="inv'+i+'"'+(o.inv?' checked':'')+'><span class="tog-lbl">Invert chain</span></div>';
        h += '</div>';
        h += '</div>';

        // Brightness (hidden for relay)
        h += '<div id="briSec'+i+'" class="field"><label class="lbl">Brightness</label>';
        h += '<input type="number" id="bri'+i+'" value="'+(o.bri==null?255:o.bri)+'" min="0" max="255"></div>';

        // Universe + channel
        h += '<div class="g2">';
        h += '<div class="field"><label class="lbl">Universe</label>';
        h += '<input type="number" id="univ'+i+'" value="'+(o.univ||0)+'" min="0" max="32768" oninput="elyonRecalc()"></div>';
        h += '<div class="field"><label class="lbl">DMX start ch</label>';
        h += '<input type="number" id="sch'+i+'" value="'+(o.ch||1)+'" min="1" max="512" oninput="elyonRecalc()"></div>';
        h += '</div>';

        // Highlight button
        h += '<button type="button" class="act-btn" style="margin-top:10px" onclick="elyonHighlight('+i+')">Highlight (white wipe)</button>';

        h += '</div></div></div>';
        return h;
    }

    // ── Section visibility / live updates ────────────────────────────────────

    window.elyonToggleCh = function (i) {
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
    };

    window.elyonProtoChange = function (i) {
        const p = parseInt(document.getElementById('proto' + i).value);
        const isPwm   = (p === P_PWM);
        const isRelay = (p === P_RELAY);
        const isClk   = isClocked(p);
        const isPx    = !isPwm && !isRelay;
        const show = (id, cond) => { const el = document.getElementById(id + i); if (el) el.style.display = cond ? '' : 'none'; };
        show('pwmSec',   isPwm);
        show('pxSec',    isPx);
        show('relaySec', isRelay);
        show('briSec',   !isRelay);
        show('clockSec', isClk);
        show('backendSec', isPx && !isClk);
        sumUpdate(i);
        elyonRecalc();
    };

    function sumUpdate(i) {
        const p = parseInt((document.getElementById('proto' + i) || {value: '1'}).value);
        const sp = document.getElementById('sProto' + i);
        if (sp) sp.textContent = PROTO_NAMES[p] || ('proto ' + p);
        const s1 = document.getElementById('sP1' + i);
        if (s1) {
            if (p === P_PWM)        s1.innerHTML = '<b>' + (document.getElementById('freq'+i)||{value:'1000'}).value + 'Hz</b>';
            else if (p === P_RELAY) s1.innerHTML = 'thr:<b>' + (document.getElementById('rthr'+i)||{value:'128'}).value + '</b>';
            else                    s1.innerHTML = '<b>' + (document.getElementById('count'+i)||{value:'0'}).value + '</b> px';
        }
        const su = document.getElementById('sU' + i);
        const sc = document.getElementById('sCH' + i);
        if (su) su.textContent = (document.getElementById('univ' + i) || {value:'0'}).value;
        if (sc) sc.textContent = (document.getElementById('sch'  + i) || {value:'1'}).value;
    }

    window.elyonRecalc = function () {
        const autoEl = document.getElementById('elyonAutoLayout');
        const auto   = autoEl ? autoEl.checked : false;
        let univ = 0, ch = 1, total = 0;
        let rmtUsed = 0, i2sUsed = 0;
        for (let i = 0; i < N; i++) {
            const protoEl = document.getElementById('proto' + i);
            if (!protoEl) continue;
            const p = parseInt(protoEl.value) || 1;
            const uEl = document.getElementById('univ' + i);
            const cEl = document.getElementById('sch' + i);
            const setUC = () => { if (auto) { if (uEl) uEl.value = univ; if (cEl) cEl.value = ch; } };

            if (p === P_RELAY) {
                setUC();
                const f0 = (ch - 1) + 1; univ += Math.floor(f0/512); ch = (f0%512)+1;
            } else if (p === P_PWM) {
                const freq = parseInt((document.getElementById('freq'+i)||{value:'1000'}).value) || 0;
                const is16 = (document.getElementById('bit16'+i)||{checked:false}).checked ? 1 : 0;
                const dmxCh = freq > 0 ? (is16?2:1) : 0;
                if (dmxCh === 0) { setUC(); sumUpdate(i); continue; }
                setUC();
                const f = (ch-1)+dmxCh; univ += Math.floor(f/512); ch = (f%512)+1;
            } else if (p === P_FOLLOWER) {
                sumUpdate(i); continue;
            } else {
                const cnt = parseInt((document.getElementById('count'+i)||{value:'0'}).value) || 0;
                const grp = parseInt((document.getElementById('grp'+i)||{value:'1'}).value) || 1;
                total += cnt;
                if (cnt > 0 && !isClocked(p)) {
                    const bkEl = document.getElementById('backend' + i);
                    const bk = bkEl ? (parseInt(bkEl.value) || 0) : (F.i2s ? 1 : 0);
                    if (bk === 1) i2sUsed++; else rmtUsed++;
                }
                if (cnt === 0) { setUC(); sumUpdate(i); continue; }
                setUC();
                const slots = Math.ceil(cnt / grp);
                const dmxCh2 = slots * chPerPixel(p);
                const f2 = (ch-1)+dmxCh2; univ += Math.floor(f2/512); ch = (f2%512)+1;
            }
            sumUpdate(i);
        }
        const cap = F.i2s ? 8192 : 4096;
        const pct = Math.min(100, (total / cap) * 100);
        const fill = document.getElementById('budFill');
        if (fill) {
            fill.style.width = pct + '%';
            fill.style.background = pct > 90 ? '#ff5533' : pct > 70 ? '#ff9900' : '#e9ff00';
        }
        const bv = document.getElementById('budVal');
        if (bv) {
            let txt = total + ' / ' + cap + (total > cap ? ' ⚠' : '');
            if (F.i2s) txt += ' · RMT: ' + rmtUsed + '/8 · I2S: ' + i2sUsed + '/8' + (rmtUsed > 8 || i2sUsed > 8 ? ' ⚠' : '');
            bv.textContent = txt;
        }
    };

    function autoLayoutChanged() {
        const auto = (document.getElementById('elyonAutoLayout') || {checked:false}).checked;
        for (let i = 0; i < N; i++) {
            const u = document.getElementById('univ' + i);
            const c = document.getElementById('sch'  + i);
            if (u) { u.readOnly = auto; u.style.opacity = auto ? '.4' : '1'; }
            if (c) { c.readOnly = auto; c.style.opacity = auto ? '.4' : '1'; }
        }
        window.elyonRecalc();
    }

    window.elyonHighlight = function (i) {
        const fd = new FormData(); fd.append('out', i);
        fetch('/ledhighlight', {method: 'POST', body: fd}).catch(() => {});
    };

    // ── Serialise back to /api/config shape ──────────────────────────────────

    window.getFixtureData = function (features) {
        const outputs = [];
        for (let i = 0; i < N; i++) {
            const protoEl = document.getElementById('proto' + i);
            if (!protoEl) { outputs.push({proto: 1, count: 0, univ: 0, ch: 1}); continue; }
            const p = parseInt(protoEl.value);
            const o = {
                proto: p,
                univ:  parseInt(getValue('univ'+i)) || 0,
                ch:    parseInt(getValue('sch'+i)) || 1,
                inv:   getChecked('inv'+i) ? 1 : 0,
            };
            if (p === P_RELAY) {
                o.relay_thr = parseInt(getValue('rthr'+i)) || 128;
                o.relay_inv = getChecked('rinv'+i) ? 1 : 0;
            } else if (p === P_PWM) {
                o.pwm_freq  = parseInt(getValue('freq'+i)) || 0;
                o.pwm_curve = parseInt(getValue('curve'+i)) || 0;
                o.pwm_16bit = getChecked('bit16'+i) ? 1 : 0;
                o.pwm_inv   = getChecked('pinv'+i)  ? 1 : 0;
                o.bri       = parseInt(getValue('bri'+i)) || 255;
            } else if (p === P_FOLLOWER) {
                // No further fields — owner is implied by the clocked partner.
            } else {
                o.count = parseInt(getValue('count'+i)) || 0;
                o.group = parseInt(getValue('grp'+i)) || 1;
                o.bri   = parseInt(getValue('bri'+i)) || 255;
                o.order = (getValue('order'+i) || '').toUpperCase();
                if (isClocked(p)) {
                    o.clock_p = parseInt(getValue('clkp'+i)) || 0;
                } else if (features.i2s) {
                    const b = parseInt(getValue('backend'+i));
                    if (!isNaN(b)) o.be = b;
                }
            }
            outputs.push(o);
        }
        return {outputs: outputs};
    };

    function getValue(id) { const el = document.getElementById(id); return el ? el.value : ''; }
    function getChecked(id) { const el = document.getElementById(id); return el ? el.checked : false; }
})();
