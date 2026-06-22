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

        let html = '';
        html += '<div class="acc-wrap"><div class="acc-body open"><div class="acc-inner">';
        html += '  <p class="field-note">Axon bridges the ArtNet/sACN universe set above into a single RS-485 DMX-512 stream on the XLR output. The 2 LED outputs below are optional pixel/PWM/relay taps that read from the same universe pool.</p>';
        html += '  <div class="tog-row" style="margin-top:8px">';
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
