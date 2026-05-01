#pragma once

// HTML accordion injected into index.html for the Veyron fixture.
// Placeholders ({{...}}) are replaced by webserver_manager.cpp at serve time.
// data-module attributes drive JS visibility based on firmware feature flags.
static const char VEYRON_FIXTURE_HTML[] = R"rawhtml(
<div class="accordion">
  <button type="button" class="accordion-button">Veyron Fixture</button>
  <div class="accordion-content">

    <button type="button" onclick="startHighlight()">Highlight / Locate</button>

    <p class="group-label">Pixel Addressing</p>
    <label for="RGBWstartAddress">RGB Pixel Start Address</label>
    <input type="number" id="RGBWstartAddress" name="RGBWstartAddress" min="1" max="512" value="{{rgbw_start_address}}" oninput="updateAddress()">

    <label for="WhStartAddress">White CW Pixel Start Address</label>
    <input type="number" id="WhStartAddress" name="WhStartAddress" min="1" max="512" value="{{wh_start_address}}">

    <label for="strobeStartAddress">Strobe DMX Start Address</label>
    <input type="number" id="strobeStartAddress" name="strobeStartAddress" min="1" max="512" value="{{strobe_start_address}}">

    <p class="group-label">Personality</p>
    <label for="personality">DMX Personality</label>
    <select id="personality" name="personality" onchange="updateAddress(); updatePersonalityDescription();">
      <option value="1" {{personality1_selected}}>Full (128ch)</option>
      <option value="2" {{personality2_selected}}>Base (126ch)</option>
      <option value="3" {{personality3_selected}}>Simple (6ch)</option>
      <option value="4" {{personality4_selected}}>Full RGB Mirror (68ch)</option>
      <option value="5" {{personality5_selected}}>Half RGB Group (68ch)</option>
    </select>
    <div id="personalityDescription" class="personality-description"></div>

    <p class="group-label">Dimming</p>
    <label for="dimCurves">Dimming Curve</label>
    <select id="dimCurves" name="dimCurves">
      <option value="1" {{LINEAR}}>Linear</option>
      <option value="2" {{SQUARE}}>Square</option>
      <option value="3" {{INVERSE_SQUARE}}>Inverse Square</option>
      <option value="4" {{S_CURVE}}>S Curve</option>
    </select>

  </div>
</div>
)rawhtml";

static const char VEYRON_FIXTURE_JS[] = R"js(
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
  document.getElementById('personalityDescription').innerHTML =
    descs[p] ? '<strong>Personality:</strong><br>' + descs[p] : '';
}
updatePersonalityDescription();
)js";
