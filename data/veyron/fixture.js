// Veyron fixture renderer — pixel bar (40 px WS2811 + 6 P9813 accents + strobe).
// Picks a personality, sets the three DMX block start addresses, and selects a
// dimming curve. The accordion structure mirrors the legacy VEYRON_FIXTURE_HTML.

(function () {
    const PERSONALITIES = {
        1: 'Full Pixel (Legacy) (128 ch)',
        2: 'Full Pixel + Macro (133 ch)',
        3: 'Full Pixel (126 ch)',
        4: 'Mirror + Macro (73 ch)',
        5: 'Mirror (66 ch)',
        6: 'Grouped 2px + Macro (73 ch)',
        7: 'Grouped 2px (66 ch)',
        8: 'RGBW + Macro (10 ch)',
        9: 'RGBW (4 ch)',
    };
    const PERSONALITY_DESCS = {
        1: '40 Pixel RGB (120 ch) + 6 White (6 ch) + Shutter Strip/Accent — frozen simple layout, no Master Dimmer/Macro, kept stable for already-patched consoles',
        2: '40 Pixel RGB (120 ch) + 6 White (6 ch) + Shutter Strip/Accent + Master Dimmer Strip/Accent + Pixel Macro + White Macro + Speed — Pixel Macro preserves each pixel\'s own patched color while it moves',
        3: '40 Pixel RGB (120 ch) + 6 White (6 ch) — bare pixel-only tier, no shutter/dimmer/macro',
        4: 'Mirror 20 Pixel RGB (60 ch) + 6 White (6 ch) + Shutter Strip/Accent + Master Dimmer Strip/Accent + Zone Macro + White Macro + Speed — Zone Macro preserves each zone\'s own patched color while it moves',
        5: 'Mirror 20 Pixel RGB (60 ch) + 6 White (6 ch) — bare pixel-only tier, no shutter/dimmer/macro',
        6: 'Grouped 20 Pixel RGB (60 ch) + 6 White (6 ch) + Shutter Strip/Accent + Master Dimmer Strip/Accent + Zone Macro + White Macro + Speed — same Zone Macro engine as Mirror',
        7: 'Grouped 20 Pixel RGB (60 ch) + 6 White (6 ch) — bare pixel-only tier, no shutter/dimmer/macro',
        8: 'Single RGB (3 ch) + Single White (1 ch) + Shutter Strip/Accent + single Master Intensity + RGB Macro + White Macro + Speed — macro idle = manual broadcast, engaged = animated pattern colored from the RGB channel',
        9: 'Single RGB (3 ch) + Single White (1 ch), no shutter, no dimmer',
    };
    // Strip/accent/function channel widths per personality, mirroring
    // include/fixtures/veyron/personalities.h — used to auto-derive the
    // White/Function start addresses (and show the function block's channel
    // gap) from the RGB start address below. Function width is 0 for the
    // bare tiers (no shutter/dimmer/macro channels at all).
    const STRIP_W    = { 1: 120, 2: 120, 3: 120, 4: 60, 5: 60, 6: 60, 7: 60, 8: 3, 9: 3 };
    const ACCENT_W   = { 1: 6,   2: 6,   3: 6,   4: 6,  5: 6,  6: 6,  7: 6,  8: 1, 9: 1 };
    const FUNCTION_W = { 1: 2,   2: 7,   3: 0,   4: 7,  5: 0,  6: 7,  7: 0,  8: 6, 9: 0 };

    // Per-personality channel rows, mirroring include/fixtures/veyron/personalities.h
    // row-for-row. sec = which start address (rgbw/white/func) the row's base
    // comes from; perUnit=3 rows are RGB pixel/zone blocks, shown as one
    // compact range instead of one row per pixel; perUnit=1 rows are single
    // function channels shown individually (or as a compact "1-6" range for
    // the repeated Accent White block).
    const PERS_ROWS = {
        1: [
            { sec: 'rgbw',  label: 'Strip Pixels',    count: 40, perUnit: 3 },
            { sec: 'white', label: 'Accent White 1-6', count: 6,  perUnit: 1 },
            { sec: 'func',  label: 'Shutter Strip',    count: 1,  perUnit: 1 },
            { sec: 'func',  label: 'Shutter Accent',   count: 1,  perUnit: 1 },
        ],
        2: [
            { sec: 'rgbw',  label: 'Strip Pixels',    count: 40, perUnit: 3 },
            { sec: 'white', label: 'Accent White 1-6', count: 6,  perUnit: 1 },
            { sec: 'func',  label: 'Shutter Strip',    count: 1,  perUnit: 1 },
            { sec: 'func',  label: 'Shutter Accent',   count: 1,  perUnit: 1 },
            { sec: 'func',  label: 'Master Dimmer Strip',  count: 1, perUnit: 1 },
            { sec: 'func',  label: 'Master Dimmer Accent', count: 1, perUnit: 1 },
            { sec: 'func',  label: 'Pixel Macro',      count: 1,  perUnit: 1 },
            { sec: 'func',  label: 'White Macro',      count: 1,  perUnit: 1 },
            { sec: 'func',  label: 'Macro Speed',      count: 1,  perUnit: 1 },
        ],
        3: [
            { sec: 'rgbw',  label: 'Strip Pixels',    count: 40, perUnit: 3 },
            { sec: 'white', label: 'Accent White 1-6', count: 6,  perUnit: 1 },
        ],
        4: [
            { sec: 'rgbw',  label: 'Strip Mirror (20 zones)', count: 20, perUnit: 3 },
            { sec: 'white', label: 'Accent White 1-6', count: 6,  perUnit: 1 },
            { sec: 'func',  label: 'Shutter Strip',    count: 1,  perUnit: 1 },
            { sec: 'func',  label: 'Shutter Accent',   count: 1,  perUnit: 1 },
            { sec: 'func',  label: 'Master Dimmer Strip',  count: 1, perUnit: 1 },
            { sec: 'func',  label: 'Master Dimmer Accent', count: 1, perUnit: 1 },
            { sec: 'func',  label: 'Zone Macro',       count: 1,  perUnit: 1 },
            { sec: 'func',  label: 'White Macro',      count: 1,  perUnit: 1 },
            { sec: 'func',  label: 'Macro Speed',      count: 1,  perUnit: 1 },
        ],
        5: [
            { sec: 'rgbw',  label: 'Strip Mirror (20 zones)', count: 20, perUnit: 3 },
            { sec: 'white', label: 'Accent White 1-6', count: 6,  perUnit: 1 },
        ],
        6: [
            { sec: 'rgbw',  label: 'Strip Group2 (20 zones)', count: 20, perUnit: 3 },
            { sec: 'white', label: 'Accent White 1-6', count: 6,  perUnit: 1 },
            { sec: 'func',  label: 'Shutter Strip',    count: 1,  perUnit: 1 },
            { sec: 'func',  label: 'Shutter Accent',   count: 1,  perUnit: 1 },
            { sec: 'func',  label: 'Master Dimmer Strip',  count: 1, perUnit: 1 },
            { sec: 'func',  label: 'Master Dimmer Accent', count: 1, perUnit: 1 },
            { sec: 'func',  label: 'Zone Macro',       count: 1,  perUnit: 1 },
            { sec: 'func',  label: 'White Macro',      count: 1,  perUnit: 1 },
            { sec: 'func',  label: 'Macro Speed',      count: 1,  perUnit: 1 },
        ],
        7: [
            { sec: 'rgbw',  label: 'Strip Group2 (20 zones)', count: 20, perUnit: 3 },
            { sec: 'white', label: 'Accent White 1-6', count: 6,  perUnit: 1 },
        ],
        8: [
            { sec: 'rgbw',  label: 'Strip Color',  count: 1, perUnit: 3 },
            { sec: 'white', label: 'Accent White', count: 1, perUnit: 1 },
            { sec: 'func',  label: 'Shutter Strip',    count: 1, perUnit: 1 },
            { sec: 'func',  label: 'Shutter Accent',   count: 1, perUnit: 1 },
            { sec: 'func',  label: 'Master Intensity', count: 1, perUnit: 1 },
            { sec: 'func',  label: 'RGB Macro',        count: 1, perUnit: 1 },
            { sec: 'func',  label: 'White Macro',      count: 1, perUnit: 1 },
            { sec: 'func',  label: 'Macro Speed',      count: 1, perUnit: 1 },
        ],
        9: [
            { sec: 'rgbw',  label: 'Strip Color',  count: 1, perUnit: 3 },
            { sec: 'white', label: 'Accent White', count: 1, perUnit: 1 },
        ],
    };
    // Personalities with a Shutter row — used to show the shared zone
    // breakdown (mirrors dmx_fixture.cpp's decodeShutterFn()) under the map.
    const PERS_HAS_SHUTTER = { 1: true, 2: true, 4: true, 6: true, 8: true };
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
        const func        = (fix.function !== undefined) ? Number(fix.function) : 127;
        const dimCurve    = (fix.dimCurve !== undefined) ? Number(fix.dimCurve) : 1;
        const statusLed   = (fix.statusLed !== undefined) ? !!fix.statusLed : true;

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
        h += '    <label class="lbl" for="vFunction">Function Start Address</label>';
        h += '    <input type="number" id="vFunction" name="vFunction" min="1" max="512" value="' + func + '">';
        h += '  </div>';
        h += '  <p class="field-note" id="vFunctionNote">' + functionGapNote(personality, func) + '</p>';

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

        h += '  <span class="grp-lbl">Channel map</span>';
        h += '  <div id="veyronChMap" style="margin-top:2px"></div>';

        h += '  <span class="grp-lbl">Dimming</span>';

        h += '  <div class="field">';
        h += '    <label class="lbl" for="vDim">Dimming Curve</label>';
        h += '    <select id="vDim" name="vDim">';
        Object.keys(DIM_CURVES).forEach(c => {
            h += '<option value="' + c + '"' + (Number(c) === dimCurve ? ' selected' : '') + '>' + DIM_CURVES[c] + '</option>';
        });
        h += '    </select>';
        h += '  </div>';

        h += '  <span class="grp-lbl">Status LED</span>';
        h += '  <div class="tog-row">';
        h += '    <input type="checkbox" id="vStatusLed" name="vStatusLed"' + (statusLed ? ' checked' : '') + '>';
        h += '    <span class="tog-lbl">Show WiFi/Ethernet + firmware update status on the strip</span>';
        h += '  </div>';
        h += '  <p class="field-note">Dim (~10%) overlay while connecting (amber sweep), in AP fallback (magenta breathing), just after connecting (green flash), or during a firmware upload (blue progress bar). Takes over the strip from DMX for as long as it\'s showing.</p>';

        h += '</div></div></div>';

        document.getElementById('fixtureSection').innerHTML = h;
        veyronChMap();
    };

    window.veyronHighlight = function () {
        fetch('/highlight', {method: 'POST'}).catch(() => {});
    };

    // Describes the function block's channel gap for the given personality —
    // 0-width (bare tiers) means there's no shutter/dimmer/macro channel at
    // all, so patching a Function Start Address there has no effect.
    function functionGapNote(p, start) {
        const w = FUNCTION_W[p] || 0;
        if (w === 0) return 'This personality has no shutter/dimmer/macro channels — Function Start Address is unused.';
        return 'Channels ' + start + '–' + (start + w - 1) + ' (' + w + ' ch): shutter/dimmer/macro block.';
    }

    // Auto-recompute White and Function start addresses based on RGB start
    // and the chosen personality — matches the legacy updateAddress()
    // behaviour, plus refreshes the channel-gap note.
    window.veyronUpdateAddress = function () {
        const rgbw = parseInt(document.getElementById('vRgbw').value) || 0;
        const p    = document.getElementById('vPers').value;
        const wh   = rgbw + (STRIP_W[p] || 0);
        const func = wh + (ACCENT_W[p] || 0);
        document.getElementById('vWhite').value    = wh;
        document.getElementById('vFunction').value = func;
        const note = document.getElementById('vFunctionNote');
        if (note) note.textContent = functionGapNote(p, func);
        veyronChMap();
    };

    window.veyronUpdateDesc = function () {
        const p = parseInt(document.getElementById('vPers').value);
        const el = document.getElementById('vPersDesc');
        if (el) el.textContent = PERSONALITY_DESCS[p] || '';
    };

    // Shared Shutter zone breakdown — mirrors dmx_fixture.cpp's
    // decodeShutterFn()/shutterInterval() exactly. Shown once under the
    // channel map for any personality that has a Shutter row.
    const SHUTTER_ZONES = [
        ['0–10',    'Open (steady)'],
        ['11–70',   'Strobe (speed increases across zone)'],
        ['71–130',  'Random Strobe (speed increases across zone)'],
        ['131–190', 'Pulse Open (rate increases across zone)'],
        ['191–249', 'Pulse Close (rate increases across zone)'],
        ['250–255', 'Open (steady)'],
    ];

    // Rebuilds the dynamic channel-map list for the current personality +
    // start addresses — same idea as Orion's orionChMap(): a compact row per
    // logical block (RGB pixel/zone groups collapsed into one range instead
    // of one row per pixel) plus a shared byte-range sub-table for the
    // Shutter channel where applicable.
    window.veyronChMap = function () {
        const el = document.getElementById('veyronChMap');
        if (!el) return;
        const p    = parseInt(document.getElementById('vPers').value) || 1;
        const rgbw = parseInt(document.getElementById('vRgbw').value) || 1;
        const wh   = parseInt(document.getElementById('vWhite').value) || (rgbw + (STRIP_W[p] || 0));
        const func = parseInt(document.getElementById('vFunction').value) || (wh + (ACCENT_W[p] || 0));
        const bases = { rgbw: rgbw, white: wh, func: func };
        const cursor = { rgbw: rgbw, white: wh, func: func };

        const rows = PERS_ROWS[p] || [];
        let html = rows.map(r => {
            const start = cursor[r.sec];
            const total = r.count * r.perUnit;
            cursor[r.sec] = start + total;
            const end = start + total - 1;
            const range = (total > 1) ? ('CH' + start + '–' + end) : ('CH' + start);
            const kind = (r.perUnit === 3) ? (r.count > 1 ? (r.count + '× RGB') : 'RGB') : (total + ' ch');
            return '<div style="display:flex;gap:10px;font-size:12px;padding:4px 0;border-bottom:1px solid var(--line)">' +
                '<span style="color:var(--acc);font-weight:600;min-width:70px">' + range + '</span>' +
                '<span style="color:var(--txt2);flex:1">' + r.label + '</span>' +
                '<span style="color:var(--txt3)">' + kind + '</span></div>';
        }).join('');

        if (PERS_HAS_SHUTTER[p]) {
            html += '<div style="margin-top:8px;font-size:11px;color:var(--txt3)">Shutter byte ranges:</div>';
            html += SHUTTER_ZONES.map(z =>
                '<div style="display:flex;gap:10px;font-size:11px;padding:3px 0;border-bottom:1px dotted var(--line)">' +
                '<span style="color:var(--txt2);min-width:64px">' + z[0] + '</span>' +
                '<span style="color:var(--txt3)">' + z[1] + '</span></div>').join('');
        }
        el.innerHTML = html;
    };

    window.getFixtureData = function () {
        return {
            personality: parseInt(document.getElementById('vPers').value)   || 1,
            rgbw:        parseInt(document.getElementById('vRgbw').value)   || 1,
            white:       parseInt(document.getElementById('vWhite').value)  || 121,
            function:    parseInt(document.getElementById('vFunction').value) || 127,
            dimCurve:    parseInt(document.getElementById('vDim').value)    || 1,
            statusLed:   document.getElementById('vStatusLed').checked,
        };
    };
})();
