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

    <div data-module="recorder">
      <p class="group-label">Scene Recorder</p>
      <label for="sceneSelect">Scene Slot</label>
      <select id="sceneSelect">
        <option value="0">Scene 1</option>
        <option value="1">Scene 2</option>
        <option value="2">Scene 3</option>
        <option value="3">Scene 4</option>
      </select>
      <button type="button" id="recordButton" onclick="startRecording()">Start Recording</button>
      <button type="button" id="playButton" onclick="playScene()">Play Scene</button>
    </div>

  </div>
</div>
)rawhtml";
