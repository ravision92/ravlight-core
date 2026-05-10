#pragma once

// HTML accordion injected into index.html for the Veyron fixture.
// Uses acc-wrap/acc-btn/acc-body CSS classes defined in index.html.
// Placeholders ({{...}}) are replaced by webserver_manager.cpp at serve time.
static const char VEYRON_FIXTURE_HTML[] = R"rawhtml(
<div class="acc-wrap">
  <button type="button" class="acc-btn" onclick="toggleAcc(this)">
    <span>Veyron Fixture</span>
    <span class="acc-arrow">&#9661;</span>
  </button>
  <div class="acc-body">
    <div class="acc-inner">

      <button type="button" class="act-btn" style="border-radius:var(--r);padding:9px;font-size:12px" onclick="startHighlight()">Highlight / Locate</button>

      <span class="grp-lbl">Pixel Addressing</span>

      <div class="field">
        <label class="lbl" for="RGBWstartAddress">RGB Pixel Start Address</label>
        <input type="number" id="RGBWstartAddress" name="RGBWstartAddress" min="1" max="512" value="{{rgbw_start_address}}" oninput="updateAddress()">
      </div>

      <div class="field">
        <label class="lbl" for="WhStartAddress">White CW Pixel Start Address</label>
        <input type="number" id="WhStartAddress" name="WhStartAddress" min="1" max="512" value="{{wh_start_address}}">
      </div>

      <div class="field">
        <label class="lbl" for="strobeStartAddress">Strobe DMX Start Address</label>
        <input type="number" id="strobeStartAddress" name="strobeStartAddress" min="1" max="512" value="{{strobe_start_address}}">
      </div>

      <span class="grp-lbl">Personality</span>

      <div class="field">
        <label class="lbl" for="personality">DMX Personality</label>
        <select id="personality" name="personality" onchange="updateAddress();updatePersonalityDescription();">
          <option value="1" {{personality1_selected}}>Full (128ch)</option>
          <option value="2" {{personality2_selected}}>Base (126ch)</option>
          <option value="3" {{personality3_selected}}>Simple (6ch)</option>
          <option value="4" {{personality4_selected}}>Full RGB Mirror (68ch)</option>
          <option value="5" {{personality5_selected}}>Half RGB Group (68ch)</option>
        </select>
      </div>
      <p class="field-note" id="personalityDescription"></p>

      <span class="grp-lbl">Dimming</span>

      <div class="field">
        <label class="lbl" for="dimCurves">Dimming Curve</label>
        <select id="dimCurves" name="dimCurves">
          <option value="1" {{LINEAR}}>Linear</option>
          <option value="2" {{SQUARE}}>Square</option>
          <option value="3" {{INVERSE_SQUARE}}>Inverse Square</option>
          <option value="4" {{S_CURVE}}>S Curve</option>
        </select>
      </div>

    </div>
  </div>
</div>
)rawhtml";

static const char VEYRON_FIXTURE_JS[] = R"js(
function startHighlight() {
  fetch('/highlight', {method:'POST'}).catch(function(){});
}
function updateAddress() {
  const rgbw = parseInt(document.getElementById('RGBWstartAddress').value) || 0;
  const p    = document.getElementById('personality').value;
  let wh = 0, strobe = 0;
  switch (p) {
    case '1': wh = rgbw + 120; strobe = wh + 6;  break;
    case '2': wh = rgbw + 120; strobe = wh + 1;  break;
    case '3': wh = rgbw + 3;   strobe = wh + 1;  break;
    case '4':
    case '5': wh = rgbw + 60;  strobe = wh + 6;  break;
  }
  document.getElementById('WhStartAddress').value     = wh;
  document.getElementById('strobeStartAddress').value = strobe;
}
function updatePersonalityDescription() {
  const p = parseInt(document.getElementById('personality').value);
  const descs = {
    1: '40 Pixel RGB (120ch) + 6 White (6ch) + Strobe RGB + Strobe White',
    2: '40 Pixel RGB (120ch) + 6 White (6ch)',
    3: 'Single RGB (3ch) + Single White (1ch) + Strobe RGB + Strobe White',
    4: 'Mirror 20 Pixel RGB (60ch) + 6 White (6ch) + Strobe RGB + Strobe White',
    5: 'Grouped 20 Pixel RGB (60ch) + 6 White (6ch) + Strobe RGB + Strobe White',
  };
  const el = document.getElementById('personalityDescription');
  if (el) el.textContent = descs[p] || '';
}
document.addEventListener('DOMContentLoaded', function() { updatePersonalityDescription(); });
)js";
