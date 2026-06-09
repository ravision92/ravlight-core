// Elyon fixture renderer — N-output LED controller. Card rendering and read-back
// live in /output-card.js (shared with Orion's LED tab); this file owns the
// fixture-level layout (auto-layout, budget bar with RMT/I2S counters, DMX-flow
// recalc) and the wire-up of save → /api/config.

(function () {
    const OC = window.outputCard;
    let N = 0;
    let F = {};

    window.renderFixture = function (fix, features) {
        F = features;
        N = features.hw_outputs || 8;
        const outputs = (fix && fix.outputs) ? fix.outputs.slice() : [];
        while (outputs.length < N) {
            outputs.push({proto: 1, count: 0, univ: 0, ch: 1, group: 1, inv: 0, bri: 255, order: 'RGB'});
        }

        let html = '';
        html += '<div class="acc-wrap"><div class="acc-body open"><div class="acc-inner">';
        html += '  <div class="tog-row">';
        html += '    <input type="checkbox" id="elyonAutoLayout">';
        html += '    <span class="tog-lbl">Auto-calculate universes &amp; channels</span>';
        html += '  </div>';
        html += '  <div class="ch-list" id="elyonCards"></div>';
        html += '  <div class="budget">';
        html += '    <span class="bud-lbl">Pixels</span>';
        html += '    <div class="bud-bar"><div class="bud-fill" id="budFill"></div></div>';
        html += '    <span class="bud-val" id="budVal">0 / 0</span>';
        html += '  </div>';
        html += '  <div class="timer-warn" id="timerWarn"></div>';
        html += '</div></div></div>';

        document.getElementById('fixtureSection').innerHTML = html;

        const cards = document.getElementById('elyonCards');
        for (let i = 0; i < N; i++) cards.insertAdjacentHTML('beforeend', OC.render(outputs[i] || {}, i, F, N));

        // The card module dispatches to window.fixtureRecalc on edits.
        window.fixtureRecalc = elyonRecalc;

        for (let i = 0; i < N; i++) OC.protoChange(i);
        elyonRecalc();
        autoLayoutChanged();
        document.getElementById('elyonAutoLayout').addEventListener('change', autoLayoutChanged);
    };

    function elyonRecalc() {
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
    }

    function autoLayoutChanged() {
        const auto = (document.getElementById('elyonAutoLayout') || {checked: false}).checked;
        for (let i = 0; i < N; i++) {
            const u = document.getElementById('univ' + i);
            const c = document.getElementById('sch'  + i);
            if (u) { u.readOnly = auto; u.style.opacity = auto ? '.4' : '1'; }
            if (c) { c.readOnly = auto; c.style.opacity = auto ? '.4' : '1'; }
        }
        elyonRecalc();
    }

    window.getFixtureData = function (features) {
        const outputs = [];
        for (let i = 0; i < N; i++) outputs.push(OC.read(i, features));
        return {outputs: outputs};
    };
})();
