// Veyron fixture renderer — pixel bar (40 px WS2811 + 6 P9813 accents + strobe).
// Picks a personality, sets the three DMX block start addresses, and selects a
// dimming curve. The accordion structure mirrors the legacy VEYRON_FIXTURE_HTML.

(function () {
    const PERSONALITIES = {
        1: 'Full (128 ch)',
        2: 'Base (126 ch)',
        3: 'Simple (6 ch)',
        4: 'Full RGB Mirror (68 ch)',
        5: 'Half RGB Group (68 ch)',
    };
    const PERSONALITY_DESCS = {
        1: '40 Pixel RGB (120 ch) + 6 White (6 ch) + Strobe RGB + Strobe White',
        2: '40 Pixel RGB (120 ch) + 6 White (6 ch)',
        3: 'Single RGB (3 ch) + Single White (1 ch) + Strobe RGB + Strobe White',
        4: 'Mirror 20 Pixel RGB (60 ch) + 6 White (6 ch) + Strobe RGB + Strobe White',
        5: 'Grouped 20 Pixel RGB (60 ch) + 6 White (6 ch) + Strobe RGB + Strobe White',
    };
    const DIM_CURVES = {
        1: 'Linear',
        2: 'Square',
        3: 'Inverse Square',
        4: 'S Curve',
    };

    window.renderFixture = function (fix /* , features */) {
        const personality = (fix.personality !== undefined) ? Number(fix.personality) : 1;
        const rgbw        = (fix.rgbw   !== undefined) ? Number(fix.rgbw)   : 1;
        const white       = (fix.white  !== undefined) ? Number(fix.white)  : 121;
        const strobe      = (fix.strobe !== undefined) ? Number(fix.strobe) : 127;
        const dimCurve    = (fix.dimCurve !== undefined) ? Number(fix.dimCurve) : 1;

        let h = '';
        h += '<div class="acc-wrap"><div class="acc-body open"><div class="acc-inner">';

        h += '  <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="veyronHighlight()">Highlight / Locate</button>';

        h += '  <span class="grp-lbl">Pixel Addressing</span>';

        h += '  <div class="field">';
        h += '    <label class="lbl" for="vRgbw">RGB Pixel Start Address</label>';
        h += '    <input type="number" id="vRgbw" name="vRgbw" min="1" max="512" value="' + rgbw + '" oninput="veyronUpdateAddress()">';
        h += '  </div>';

        h += '  <div class="field">';
        h += '    <label class="lbl" for="vWhite">White CW Pixel Start Address</label>';
        h += '    <input type="number" id="vWhite" name="vWhite" min="1" max="512" value="' + white + '">';
        h += '  </div>';

        h += '  <div class="field">';
        h += '    <label class="lbl" for="vStrobe">Strobe DMX Start Address</label>';
        h += '    <input type="number" id="vStrobe" name="vStrobe" min="1" max="512" value="' + strobe + '">';
        h += '  </div>';

        h += '  <span class="grp-lbl">Personality</span>';

        h += '  <div class="field">';
        h += '    <label class="lbl" for="vPers">DMX Personality</label>';
        h += '    <select id="vPers" name="vPers" onchange="veyronUpdateAddress();veyronUpdateDesc();">';
        Object.keys(PERSONALITIES).forEach(p => {
            h += '<option value="' + p + '"' + (Number(p) === personality ? ' selected' : '') + '>' + PERSONALITIES[p] + '</option>';
        });
        h += '    </select>';
        h += '  </div>';
        h += '  <p class="field-note" id="vPersDesc">' + (PERSONALITY_DESCS[personality] || '') + '</p>';

        h += '  <span class="grp-lbl">Dimming</span>';

        h += '  <div class="field">';
        h += '    <label class="lbl" for="vDim">Dimming Curve</label>';
        h += '    <select id="vDim" name="vDim">';
        Object.keys(DIM_CURVES).forEach(c => {
            h += '<option value="' + c + '"' + (Number(c) === dimCurve ? ' selected' : '') + '>' + DIM_CURVES[c] + '</option>';
        });
        h += '    </select>';
        h += '  </div>';

        h += '</div></div></div>';

        document.getElementById('fixtureSection').innerHTML = h;
    };

    window.veyronHighlight = function () {
        fetch('/highlight', {method: 'POST'}).catch(() => {});
    };

    // Auto-recompute White and Strobe start addresses based on RGB start and
    // the chosen personality — matches the legacy updateAddress() behaviour.
    window.veyronUpdateAddress = function () {
        const rgbw = parseInt(document.getElementById('vRgbw').value) || 0;
        const p    = document.getElementById('vPers').value;
        let wh = 0, strobe = 0;
        switch (p) {
            case '1': wh = rgbw + 120; strobe = wh + 6;  break;
            case '2': wh = rgbw + 120; strobe = wh + 1;  break;
            case '3': wh = rgbw + 3;   strobe = wh + 1;  break;
            case '4':
            case '5': wh = rgbw + 60;  strobe = wh + 6;  break;
        }
        document.getElementById('vWhite').value  = wh;
        document.getElementById('vStrobe').value = strobe;
    };

    window.veyronUpdateDesc = function () {
        const p = parseInt(document.getElementById('vPers').value);
        const el = document.getElementById('vPersDesc');
        if (el) el.textContent = PERSONALITY_DESCS[p] || '';
    };

    window.getFixtureData = function () {
        return {
            personality: parseInt(document.getElementById('vPers').value)   || 1,
            rgbw:        parseInt(document.getElementById('vRgbw').value)   || 1,
            white:       parseInt(document.getElementById('vWhite').value)  || 121,
            strobe:      parseInt(document.getElementById('vStrobe').value) || 127,
            dimCurve:    parseInt(document.getElementById('vDim').value)    || 1,
        };
    };
})();
