// Axon fixture renderer — network DMX node + 2 LED outputs.
//
// Axon's primary role (ArtNet/sACN → RS-485 bridge) is driven by the core's
// DMX panel: the user picks "Universe" + ticks "DMX Output" up in the DMX
// accordion, and the wire is fed automatically. This fixture panel only owns
// the 2 optional LED outputs — same card layout as Elyon (uses the shared
// /output-card.js renderer), just trimmed to 2 cards.

(function () {
    const OC = window.outputCard;
    let N = 0;
    let F = {};

    window.renderFixture = function (fix, features) {
        F = features;
        N = features.hw_outputs || 2;
        const outputs = (fix && fix.outputs) ? fix.outputs.slice() : [];
        while (outputs.length < N) {
            outputs.push({proto: 1, count: 0, univ: 0, ch: 1, group: 1, inv: 0, bri: 255, order: 'RGB'});
        }

        const dmxOff = (fix && fix.out_offset != null) ? fix.out_offset : 0;

        let html = '';
        html += '<div class="acc-wrap"><div class="acc-body open"><div class="acc-inner">';
        html += '  <p class="field-note">Axon bridges the ArtNet/sACN universe set above into a single RS-485 DMX-512 stream on the XLR output. The 2 LED outputs below are optional pixel/PWM/relay taps that read from the same universe pool.</p>';

        // ── DMX OUT card — same shape as Elyon/Orion output cards ────────
        // Uses the same expand/collapse mechanic as output-card.js so the
        // animation matches: `open` class goes on the .ch-card and the
        // body's max-height is driven manually.
        html += '  <div class="ch-card open" id="card-axon-dmx" style="margin-top:10px">';
        html += '    <div class="ch-head" onclick="window.toggleAxonDmxCard()">';
        html += '      <div class="ch-id">';
        html += '        <span class="ch-num">DMX1</span>';
        html += '        <span class="ch-gpio">GPIO 33</span>';
        html += '      </div>';
        html += '      <div class="ch-sum">';
        html += '        <span class="ch-proto">RS-485</span>';
        html += '        <div class="ch-tags">';
        html += '          <span class="ch-tag">FPS:<b id="axonFps">—</b></span>';
        html += '          <span class="ch-tag">OFFSET:<b id="axonOutOffsetTag">' + dmxOff + '</b></span>';
        html += '        </div>';
        html += '      </div>';
        html += '      <span class="ch-arr">&#9661;</span>';
        html += '    </div>';
        html += '    <div class="ch-body"><div class="ch-form">';
        html += '      <div class="field">';
        html += '        <label class="lbl" for="axonOutOffset">Channel offset</label>';
        html += '        <input type="number" id="axonOutOffset" min="0" max="511" value="' + dmxOff + '"';
        html += '               oninput="var v=Math.max(0,Math.min(511,parseInt(this.value)||0));document.getElementById(\'axonOutOffsetTag\').textContent=v">';
        html += '        <p class="field-note" style="margin-top:6px">Wire ch 1 = input ch (offset+1). Channels past (512−offset) are zero-padded. Use this to daisy-chain multiple Axon nodes, each forwarding a slice of a single source universe.</p>';
        html += '      </div>';
        html += '    </div></div>';
        html += '  </div>';

        html += '  <div class="tog-row" style="margin-top:10px">';
        html += '    <input type="checkbox" id="axonAutoLayout">';
        html += '    <span class="tog-lbl">Auto-calculate universes &amp; channels</span>';
        html += '  </div>';
        html += '  <div class="ch-list" id="axonCards"></div>';
        html += '  <div class="budget">';
        html += '    <span class="bud-lbl">Pixels</span>';
        html += '    <div class="bud-bar"><div class="bud-fill" id="budFill"></div></div>';
        html += '    <span class="bud-val" id="budVal">0 / 0</span>';
        html += '  </div>';
        html += '</div></div></div>';

        document.getElementById('fixtureSection').innerHTML = html;

        const cards = document.getElementById('axonCards');
        for (let i = 0; i < N; i++) cards.insertAdjacentHTML('beforeend', OC.render(outputs[i] || {}, i, F, N));

        window.fixtureRecalc = axonRecalc;

        for (let i = 0; i < N; i++) OC.protoChange(i);
        axonRecalc();
        autoLayoutChanged();
        document.getElementById('axonAutoLayout').addEventListener('change', autoLayoutChanged);

        // Initialise the DMX1 card's body max-height so the .open default
        // state actually shows the form (the CSS rule defaults to 0).
        // Run after one paint so scrollHeight is accurate.
        requestAnimationFrame(() => {
            const c = document.getElementById('card-axon-dmx');
            if (c) {
                const b = c.querySelector('.ch-body');
                if (b) b.style.maxHeight = b.scrollHeight + 'px';
            }
        });
    };

    // Mirror of output-card.js's toggle for the DMX1 card — manipulates
    // the .open class on the card AND drives max-height on the body so
    // the CSS expand/collapse animation runs.
    window.toggleAxonDmxCard = function () {
        const c = document.getElementById('card-axon-dmx');
        if (!c) return;
        const body = c.querySelector('.ch-body');
        if (!body) return;
        const opening = !c.classList.contains('open');
        c.classList.toggle('open');
        if (opening) {
            body.style.maxHeight = body.scrollHeight + 'px';
        } else {
            body.style.maxHeight = '0';
        }
    };

    function axonRecalc() {
        const autoEl = document.getElementById('axonAutoLayout');
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

            if (p === OC.P_RELAY) {
                setUC();
                const f0 = (ch - 1) + 1; univ += Math.floor(f0 / 512); ch = (f0 % 512) + 1;
            } else if (p === OC.P_PWM) {
                const freq = parseInt((document.getElementById('freq' + i) || {value: '1000'}).value) || 0;
                const is16 = (document.getElementById('bit16' + i) || {checked: false}).checked ? 1 : 0;
                const dmxCh = freq > 0 ? (is16 ? 2 : 1) : 0;
                if (dmxCh === 0) { setUC(); OC.sumUpdate(i); continue; }
                setUC();
                const f = (ch - 1) + dmxCh; univ += Math.floor(f / 512); ch = (f % 512) + 1;
            } else if (p === OC.P_FOLLOWER) {
                OC.sumUpdate(i); continue;
            } else {
                const cnt = parseInt((document.getElementById('count' + i) || {value: '0'}).value) || 0;
                const grp = parseInt((document.getElementById('grp' + i) || {value: '1'}).value) || 1;
                total += cnt;
                if (cnt > 0 && !OC.isClocked(p)) {
                    const bkEl = document.getElementById('backend' + i);
                    const bk = bkEl ? (parseInt(bkEl.value) || 0) : (F.i2s ? 1 : 0);
                    if (bk === 1) i2sUsed++; else rmtUsed++;
                }
                if (cnt === 0) { setUC(); OC.sumUpdate(i); continue; }
                setUC();
                const slots  = Math.ceil(cnt / grp);
                const dmxCh2 = slots * OC.chPerPixel(p);
                const f2 = (ch - 1) + dmxCh2; univ += Math.floor(f2 / 512); ch = (f2 % 512) + 1;
            }
            OC.sumUpdate(i);
        }
        // No hard pixel-budget cap on Axon — only 2 outputs and they share the
        // same pool as everything else. Show the running totals for sanity.
        const cap = F.i2s ? 2048 : 1000;
        const pct = Math.min(100, (total / cap) * 100);
        const fill = document.getElementById('budFill');
        if (fill) {
            fill.style.width = pct + '%';
            fill.style.background = pct > 90 ? '#ff5533' : pct > 70 ? '#ff9900' : '#e9ff00';
        }
        const bv = document.getElementById('budVal');
        if (bv) {
            let txt = total + ' / ' + cap + (total > cap ? ' ⚠' : '');
            if (F.i2s) txt += ' · RMT: ' + rmtUsed + ' · I2S: ' + i2sUsed;
            bv.textContent = txt;
        }
    }

    function autoLayoutChanged() {
        const auto = (document.getElementById('axonAutoLayout') || {checked: false}).checked;
        for (let i = 0; i < N; i++) {
            const u = document.getElementById('univ' + i);
            const c = document.getElementById('sch'  + i);
            if (u) { u.readOnly = auto; u.style.opacity = auto ? '.4' : '1'; }
            if (c) { c.readOnly = auto; c.style.opacity = auto ? '.4' : '1'; }
        }
        axonRecalc();
    }

    window.getFixtureData = function (features) {
        const outputs = [];
        for (let i = 0; i < N; i++) outputs.push(OC.read(i, features));
        return {outputs: outputs};
    };
})();
