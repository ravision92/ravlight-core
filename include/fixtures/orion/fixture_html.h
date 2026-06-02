#pragma once
#ifdef RAVLIGHT_FIXTURE_ORION

// ── Orion Motor accordion ────────────────────────────────────────────────────
// Uses the same class system as Network / DMX / Settings accordions:
//   .acc-inner  — body wrapper with vertical gap layout
//   .field      — label+input pair
//   .lbl        — uppercase small-caps field label
//   .g2         — 2-column grid container
//   .grp-lbl    — section header (accent underline)
//   .div        — horizontal divider
//   .field-note — small caption text
//
// Tokens {V_*} are replaced server-side. Step values are pre-converted to cm.

// Common inline style for inline-row action buttons (smaller act-btn).
#define _ORION_BTN " class=\"act-btn\" style=\"border-radius:var(--r);padding:9px;font-size:12px\""
#define _ORION_BTN_DANGER " class=\"act-btn\" style=\"border-radius:var(--r);padding:9px;font-size:12px;color:var(--red);border-color:rgba(255,85,51,.35)\""

// Collapsible sub-section card helpers (shared ch-card/ch-head/ch-body CSS).
// _OC_OPEN  begins a card: title + summary span id + toggle head.
// _OC_MID   closes the head, opens the body form.
// _OC_END   closes body + card.
#define _OC_OPEN(title, sumid) \
  "<div class=\"ch-card\">" \
    "<div class=\"ch-head\" style=\"grid-template-columns:1fr 24px\" onclick=\"orionToggle(this)\">" \
      "<div class=\"ch-sum\">" \
        "<span class=\"ch-proto\">" title "</span>" \
        "<div class=\"ch-tags\"><span class=\"ch-tag\" id=\"" sumid "\"></span></div>" \
      "</div>" \
      "<span class=\"ch-arr\">&#9661;</span>" \
    "</div>" \
    "<div class=\"ch-body\"><div class=\"ch-form\">"
#define _OC_END "</div></div></div>"

#define ORION_FIXTURE_HTML \
  "<div class=\"acc-wrap\">" \
  "<button type=\"button\" class=\"acc-btn open\" onclick=\"toggleAcc(this)\">" \
    "<span>Orion</span>" \
    "<span class=\"acc-arrow\">&#9661;</span>" \
  "</button>" \
  "<div class=\"acc-body open\">" \
    "<div class=\"acc-inner\">" \
      /* Status panel */ \
      "<div id=\"orionStatus\" style=\"display:flex;flex-wrap:wrap;gap:10px;align-items:center;font-size:12px;color:var(--txt2)\">" \
        "<span id=\"orionState\" style=\"padding:4px 10px;border-radius:6px;background:#444;color:#fff;font-weight:600;font-size:11px;letter-spacing:.04em;text-transform:uppercase\">--</span>" \
        "<span>pos: <b id=\"orionPosCm\" style=\"color:var(--txt)\">--</b> cm</span>" \
        "<span>sg: <b id=\"orionSg\" style=\"color:var(--txt)\">--</b></span>" \
        "<span>temp: <b id=\"orionTemp\" style=\"color:var(--txt)\">--</b>&deg;C</span>" \
        "<span id=\"orionHomed\" style=\"color:var(--txt3)\">not homed</span>" \
        "<span id=\"orionFaults\" style=\"color:#c44\"></span>" \
      "</div>" \
      /* Action buttons */ \
      "<div style=\"display:flex;gap:6px\">" \
        "<button type=\"button\"" _ORION_BTN " onclick=\"orionPost('/home')\">Home</button>" \
        "<button type=\"button\"" _ORION_BTN " onclick=\"orionPost('/clearfault')\">Clear Fault</button>" \
        "<button type=\"button\"" _ORION_BTN_DANGER " onclick=\"orionPost('/estop')\">E-stop</button>" \
        "<button type=\"button\"" _ORION_BTN " onclick=\"orionPost('/highlight')\">Highlight</button>" \
      "</div>" \
      "<div style=\"height:6px\"></div>" \
      "<div class=\"ch-list\">" \
      /* 1. DMX patch */ \
      _OC_OPEN("DMX patch", "oSumDmx") \
        "<div class=\"field\">" \
          "<label class=\"lbl\" for=\"personality\">Personality</label>" \
          "<select id=\"personality\" name=\"personality\">" \
            "<option value=\"1\" {SEL_P1}>1 - Basic (4ch, 8-bit pos)</option>" \
            "<option value=\"2\" {SEL_P2}>2 - Basic HD (5ch, 16-bit pos)</option>" \
            "<option value=\"3\" {SEL_P3}>3 - Standard (6ch, +accel)</option>" \
          "</select>" \
        "</div>" \
        "<div class=\"g2\">" \
          "<div class=\"field\">" \
            "<label class=\"lbl\" for=\"positionStart\">Position start (MSB)</label>" \
            "<input type=\"number\" id=\"positionStart\" name=\"positionStart\" min=\"1\" max=\"511\" value=\"{V_POSITION_START}\">" \
          "</div>" \
          "<div class=\"field\">" \
            "<label class=\"lbl\" for=\"controlStart\">Control start (Enable)</label>" \
            "<input type=\"number\" id=\"controlStart\" name=\"controlStart\" min=\"1\" max=\"510\" value=\"{V_CONTROL_START}\">" \
          "</div>" \
        "</div>" \
        "<div class=\"div\" style=\"margin:2px 0\"></div>" \
        "<span class=\"lbl\">Channel map</span>" \
        "<div id=\"orionChMap\" style=\"margin-top:2px\"></div>" \
        "<p class=\"field-note\">Position and Control are independent addresses inside the motor universe above.</p>" \
      _OC_END \
      /* 2. Homing */ \
      _OC_OPEN("Homing", "oSumHome") \
        "<div class=\"field\">" \
          "<label class=\"lbl\" for=\"homingDirection\">Direction</label>" \
          "<select id=\"homingDirection\" name=\"homingDirection\">" \
            "<option value=\"-1\" {SEL_DN}>Down (-)</option>" \
            "<option value=\"1\" {SEL_UP}>Up (+)</option>" \
          "</select>" \
        "</div>" \
        "<p class=\"field-note\">Homing detects the end stop via StallGuard. The operating threshold is tuned to your real hung load with the calibration wizard.</p>" \
        "<div style=\"display:flex;gap:6px;margin-top:6px\">" \
          "<button type=\"button\" data-orion-needsdisarm" _ORION_BTN " onclick=\"orionCalOpen()\">Calibrate StallGuard</button>" \
        "</div>" \
        "<p class=\"field-note\">Current stall threshold (SGTHRS): <b id=\"orionSgthVal\">--</b></p>" \
      _OC_END \
      /* 3. Manual jog */ \
      _OC_OPEN("Manual jog (calibration)", "oSumJog") \
        "<div class=\"field\">" \
          "<label class=\"lbl\" for=\"jogSpeedCm\">Jog speed (cm/s)</label>" \
          "<input type=\"number\" id=\"jogSpeedCm\" name=\"jogSpeedCm\" step=\"0.1\" min=\"0.1\" value=\"{V_JOG_SPEED_CM}\">" \
        "</div>" \
        "<div style=\"display:flex;gap:6px\">" \
          "<button type=\"button\" id=\"orionJogUp\"" _ORION_BTN ">&#x25B2; Jog Up (hold)</button>" \
          "<button type=\"button\" id=\"orionJogDown\"" _ORION_BTN ">&#x25BC; Jog Down (hold)</button>" \
          "<button type=\"button\" id=\"orionReleaseBtn\"" _ORION_BTN " onclick=\"orionReleaseDmx()\" disabled>Release to DMX</button>" \
        "</div>" \
        "<p class=\"field-note\" id=\"orionJogNote\">Hold a jog button to move. Once you jog, DMX position commands are suspended until you click <b>Release to DMX</b>. Jog is disabled while the console has Enable &ne; 0.</p>" \
      _OC_END \
      /* 4. Travel limits */ \
      _OC_OPEN("Travel limits", "oSumTravel") \
        "<div class=\"g2\">" \
          "<div class=\"field\">" \
            "<label class=\"lbl\" for=\"downCm\">DOWN limit (cm)</label>" \
            "<input type=\"number\" id=\"downCm\" name=\"downCm\" step=\"0.1\" value=\"{V_DOWN_CM}\">" \
          "</div>" \
          "<div class=\"field\">" \
            "<label class=\"lbl\" for=\"upCm\">UP limit (cm)</label>" \
            "<input type=\"number\" id=\"upCm\" name=\"upCm\" step=\"0.1\" value=\"{V_UP_CM}\">" \
          "</div>" \
        "</div>" \
        "<div class=\"g2\">" \
          "<div class=\"field\">" \
            "<label class=\"lbl\" for=\"maxSpeedCm\">Max speed (cm/s)</label>" \
            "<input type=\"number\" id=\"maxSpeedCm\" name=\"maxSpeedCm\" step=\"0.1\" min=\"0.1\" value=\"{V_MAX_SPEED_CM}\">" \
          "</div>" \
          "<div class=\"field\">" \
            "<label class=\"lbl\" for=\"maxAccelCm\">Max accel (cm/s&sup2;)</label>" \
            "<input type=\"number\" id=\"maxAccelCm\" name=\"maxAccelCm\" step=\"0.1\" min=\"0.1\" value=\"{V_MAX_ACCEL_CM}\">" \
          "</div>" \
        "</div>" \
        "<div style=\"display:flex;gap:6px\">" \
          "<button type=\"button\" data-orion-needsdisarm" _ORION_BTN " onclick=\"orionSetLimit('down')\">Set as DOWN limit</button>" \
          "<button type=\"button\" data-orion-needsdisarm" _ORION_BTN " onclick=\"orionSetLimit('up')\">Set as UP limit</button>" \
        "</div>" \
        "<p class=\"field-note\">Travel: <b id=\"orionTravelCm\">--</b> <span id=\"orionTravelU\">cm</span>. DMX 0 maps to DOWN, DMX max to UP &mdash; regardless of step direction. Captured limits persist immediately.</p>" \
      _OC_END \
      /* 5. Safety */ \
      _OC_OPEN("Safety", "oSumSafety") \
        "<div class=\"field\">" \
          "<label class=\"lbl\" for=\"dmxWatchdogAction\">On DMX loss</label>" \
          "<select id=\"dmxWatchdogAction\" name=\"dmxWatchdogAction\">" \
            "<option value=\"0\" {SEL_WDE}>E-stop</option>" \
            "<option value=\"1\" {SEL_WDH}>Return to home (slow)</option>" \
          "</select>" \
        "</div>" \
      _OC_END \
      /* 6. Mechanical calibration */ \
      _OC_OPEN("Mechanical calibration", "oSumMech") \
        "<div class=\"field\">" \
          "<label class=\"lbl\" for=\"orionUnitSel\">Display units</label>" \
          "<select id=\"orionUnitSel\">" \
            "<option value=\"cm\">Centimeters (cm)</option>" \
            "<option value=\"in\">Inches (in)</option>" \
          "</select>" \
        "</div>" \
        "<div class=\"g2\">" \
          "<div class=\"field\">" \
            "<label class=\"lbl\" for=\"drumDiaMm\">Drum diameter (mm)</label>" \
            "<input type=\"number\" id=\"drumDiaMm\" name=\"drumDiaMm\" min=\"1\" max=\"2000\" value=\"{V_DRUM_MM}\">" \
          "</div>" \
          "<div class=\"field\">" \
            "<label class=\"lbl\" for=\"gearRatio\">Gear ratio (motor:drum)</label>" \
            "<input type=\"number\" id=\"gearRatio\" name=\"gearRatio\" step=\"0.01\" min=\"0.01\" value=\"{V_GEAR}\">" \
          "</div>" \
        "</div>" \
        "<div class=\"field\" style=\"margin-top:8px\">" \
          "<label class=\"lbl\" for=\"motorStepsRev\">Motor steps / revolution</label>" \
          "<input type=\"number\" id=\"motorStepsRev\" name=\"motorStepsRev\" min=\"1\" max=\"10000\" value=\"{V_STEPS_REV}\">" \
        "</div>" \
        "<p class=\"field-note\">Resolution: <b id=\"orionResVal\">{V_STEPS_PER_CM}</b> <span id=\"orionResU\">steps/cm</span> &mdash; sets the scale for every distance here. Microstepping fixed at 16.</p>" \
      _OC_END \
      "</div>" \
      /* StallGuard calibration wizard (body + buttons filled by JS) */ \
      "<div class=\"modal-overlay\" id=\"orionCalModal\">" \
        "<div class=\"modal-box\" style=\"text-align:left\">" \
          "<h3>StallGuard calibration</h3>" \
          "<div id=\"orionCalBody\" style=\"font-size:13px;color:var(--txt2);line-height:1.5\"></div>" \
          "<div class=\"modal-btns\" id=\"orionCalBtns\"></div>" \
        "</div>" \
      "</div>" \
    "</div>" \
  "</div>" \
  "</div>"

#define ORION_FIXTURE_JS \
  "function orionPost(url,body){" \
    "var opts={method:'POST'};if(body){opts.body=body;}" \
    "return fetch(url,opts).then(r=>r.text());" \
  "}" \
  "function orionStateColor(s){" \
    "return {idle:'#3a3',moving:'#36c',homing:'#c93',jogging:'#c93',fault:'#c44',driver_off:'#666'}[s]||'#444';" \
  "}" \
  /* Collapsible sub-section toggle (animated, like Elyon cards) */ \
  "function orionToggle(h){" \
    "var c=h.parentNode,body=c.querySelector('.ch-body');if(!body)return;" \
    "var opening=!c.classList.contains('open');c.classList.toggle('open');" \
    "if(opening){body.style.maxHeight=body.scrollHeight+'px';" \
      "body.addEventListener('transitionend',function fn(){body.style.maxHeight='none';body.removeEventListener('transitionend',fn);},{once:true});" \
    "}else{body.style.maxHeight=body.scrollHeight+'px';" \
      "requestAnimationFrame(function(){requestAnimationFrame(function(){body.style.maxHeight='0';});});}" \
  "}" \
  /* Summary helpers — keep card headers in sync with the form fields */ \
  "function orionV(id,d){var e=document.getElementById(id);return e?e.value:d;}" \
  "function orionPName(p){return {'1':'Basic','2':'Basic HD','3':'Standard'}[p]||p;}" \
  "function orionStepsPerCmCalc(){" \
    "var sr=parseFloat(orionV('motorStepsRev','200'))||200;" \
    "var gr=parseFloat(orionV('gearRatio','1'))||1;" \
    "var dd=parseFloat(orionV('drumDiaMm','30'))||30;" \
    "var circ=Math.PI*(dd/10);if(circ<0.1)return 1;" \
    "return sr*16*gr/circ;" \
  "}" \
  "function orionSums(){" \
    "var inch=(orionUnit==='in'),du=inch?'in':'cm';" \
    "var cv=function(x){return(inch?x/2.54:x).toFixed(1);};" \
    "var s=document.getElementById('oSumDmx');" \
    "if(s)s.innerHTML=orionPName(orionV('personality','1'))+' &middot; Pos <b>'+orionV('positionStart','1')+'</b> &middot; Ctrl <b>'+orionV('controlStart','3')+'</b>';" \
    "s=document.getElementById('oSumHome');" \
    "if(s)s.innerHTML='Dir <b>'+(parseInt(orionV('homingDirection','-1'))>=0?'Up':'Down')+'</b>';" \
    "s=document.getElementById('oSumJog');" \
    "if(s)s.innerHTML='<b>'+cv(parseFloat(orionV('jogSpeedCm','0'))||0)+'</b> '+du+'/s';" \
    "s=document.getElementById('oSumTravel');" \
    "if(s)s.innerHTML='D <b>'+cv(parseFloat(orionV('downCm','0'))||0)+'</b> &middot; U <b>'+cv(parseFloat(orionV('upCm','0'))||0)+'</b> '+du;" \
    "s=document.getElementById('oSumSafety');" \
    "if(s)s.innerHTML='On loss: <b>'+(parseInt(orionV('dmxWatchdogAction','0'))===1?'Return home':'E-stop')+'</b>';" \
    "var spc=orionStepsPerCmCalc();window.orionResBase=spc;" \
    "s=document.getElementById('oSumMech');" \
    "if(s)s.innerHTML='<b>'+(inch?spc*2.54:spc).toFixed(1)+'</b> '+(inch?'steps/in':'steps/cm');" \
    "var rv=document.getElementById('orionResVal');if(rv)rv.textContent=(inch?spc*2.54:spc).toFixed(1);" \
    "var ru=document.getElementById('orionResU');if(ru)ru.textContent=inch?'steps/in':'steps/cm';" \
    "orionChMap();" \
  "}" \
  /* Live channel map for the DMX patch card — follows personality + start addresses */ \
  "function orionChMap(){" \
    "var el=document.getElementById('orionChMap');if(!el)return;" \
    "var p=parseInt(orionV('personality','1'))||1;" \
    "var ps=parseInt(orionV('positionStart','1'))||1;" \
    "var cs=parseInt(orionV('controlStart','3'))||3;" \
    "var rows=[];" \
    "if(p===1){rows.push([ps,'Position (8-bit)']);}else{rows.push([ps,'Position MSB']);rows.push([ps+1,'Position LSB']);}" \
    "rows.push([cs,'Enable']);rows.push([cs+1,'Speed']);" \
    "if(p===3){rows.push([cs+2,'Acceleration']);rows.push([cs+3,'Function']);}else{rows.push([cs+2,'Function']);}" \
    "rows.sort(function(a,b){return a[0]-b[0];});" \
    "el.innerHTML=rows.map(function(r){" \
      "return '<div style=\"display:flex;gap:10px;font-size:12px;padding:4px 0;border-bottom:1px solid var(--line)\">'+" \
        "'<span style=\"color:var(--acc);font-weight:600;min-width:38px\">CH'+r[0]+'</span>'+" \
        "'<span style=\"color:var(--txt2)\">'+r[1]+'</span></div>';" \
    "}).join('');" \
  "}" \
  /* LED output card summary (proto / px / universe / channel) */ \
  "function orionLedSum(i){" \
    "var PN=['WS2811','WS2812B','SK6812 RGBW','WS2814 RGBW','WS2815','TM1814 RGBW','TM1914 RGBW'];" \
    "var pr=document.getElementById('oLedProtoSel'+i);" \
    "var sp=document.getElementById('oLedProto'+i);" \
    "if(pr&&sp)sp.textContent=PN[parseInt(pr.value)]||pr.value;" \
    "var px=document.getElementById('oLedPx'+i),cn=document.getElementById('oLedCount'+i);" \
    "if(px&&cn)px.innerHTML='<b>'+(cn.value||'0')+'</b> px';" \
    "var su=document.getElementById('oLedU'+i),un=document.getElementById('oLedUniv'+i);" \
    "if(su&&un)su.textContent=un.value;" \
    "var sc=document.getElementById('oLedCh'+i),ch=document.getElementById('oLedChI'+i);" \
    "if(sc&&ch)sc.textContent=ch.value;" \
  "}" \
  "function orionLedHighlight(i){var fd=new FormData();fd.append('out',i);fetch('/ledhighlight',{method:'POST',body:fd}).catch(function(){});}" \
  "function orionJogStart(dir){" \
    "var fd=new FormData();fd.append('dir',dir);" \
    "orionPost('/jog',fd).then(orionRefresh);" \
  "}" \
  "function orionJogStop(){orionPost('/jogstop');}" \
  "function orionReleaseDmx(){orionPost('/release-dmx').then(orionRefresh);}" \
  /* ── StallGuard calibration wizard ──────────────────────────────────────── */ \
  "var orionCal={step:0,dir:-1,free:null,loaded:null,sampling:false,smin:null,ssum:0,scnt:0,timer:null};" \
  "function orionCalOpen(){" \
    "orionCal={step:0,dir:(parseInt(orionV('homingDirection','-1'))>=0?1:-1),free:null,loaded:null,sampling:false,smin:null,ssum:0,scnt:0,timer:null};" \
    "orionCalRender();document.getElementById('orionCalModal').classList.add('open');" \
  "}" \
  "function orionCalClose(){orionCalSampleStop();document.getElementById('orionCalModal').classList.remove('open');}" \
  "function orionCalGoto(s){orionCal.step=s;orionCalRender();}" \
  "function orionCalSampleStart(){" \
    "if(orionCal.sampling)return;" \
    "orionCal.sampling=true;orionCal.smin=null;orionCal.ssum=0;orionCal.scnt=0;" \
    "var fd=new FormData();fd.append('dir',orionCal.dir>0?'up':'down');" \
    "fetch('/sgcal',{method:'POST',body:fd}).catch(function(){});" \
    "orionCal.timer=setInterval(orionCalPoll,150);" \
  "}" \
  "function orionCalPoll(){" \
    "fetch('/motorstatus').then(function(r){return r.json();}).then(function(d){" \
      "if(d.sgResult==null)return;var v=d.sgResult;" \
      "if(orionCal.smin===null||v<orionCal.smin)orionCal.smin=v;" \
      "orionCal.ssum+=v;orionCal.scnt++;" \
      "var el=document.getElementById('orionCalLive');" \
      "if(el)el.innerHTML='sg now <b>'+v+'</b> &middot; samples '+orionCal.scnt;" \
    "}).catch(function(){});" \
  "}" \
  "function orionCalSampleStop(){" \
    "if(!orionCal.sampling)return;orionCal.sampling=false;" \
    "if(orionCal.timer){clearInterval(orionCal.timer);orionCal.timer=null;}" \
    "fetch('/sgcalstop',{method:'POST'}).catch(function(){});" \
    "var avg=orionCal.scnt?Math.round(orionCal.ssum/orionCal.scnt):null;" \
    "if(avg!=null){if(orionCal.step===1)orionCal.free=avg;else if(orionCal.step===2)orionCal.loaded=avg;}" \
    "orionCalRender();" \
  "}" \
  "function orionCalThreshold(){" \
    "if(orionCal.free==null||orionCal.loaded==null)return null;" \
    "var t=Math.round((orionCal.free+orionCal.loaded)/2);if(t<1)t=1;if(t>255)t=255;return t;" \
  "}" \
  "function orionCalSave(){" \
    "var t=orionCalThreshold();if(t==null)return;" \
    "var fd=new FormData();fd.append('value',t);" \
    "fetch('/sgthreshold',{method:'POST',body:fd}).then(function(r){return r.text();}).then(function(){" \
      "if(typeof showToast==='function')showToast('Stall threshold saved: '+t);orionCalClose();" \
    "}).catch(function(){if(typeof showToast==='function')showToast('Save failed');});" \
  "}" \
  "function orionCalMeasureBtn(){return '<button type=\"button\" id=\"orionCalMeasure\" class=\"act-btn\" style=\"margin-top:10px;padding:12px;font-size:13px\">Hold to Measure</button>';}" \
  "function orionCalRender(){" \
    "var b=document.getElementById('orionCalBody'),btn=document.getElementById('orionCalBtns');if(!b||!btn)return;" \
    "var dirTxt=orionCal.dir>0?'Up (+)':'Down (-)';" \
    "var rate=(window.orionHomingSpeed&&window.orionResBase)?(window.orionHomingSpeed/window.orionResBase):null;" \
    "var rateTxt=rate?(' (~'+rate.toFixed(1)+' cm/s)'):'';" \
    "if(orionCal.step===0){" \
      "b.innerHTML='<p>This tunes the stall threshold against your real load. The motor moves slowly'+rateTxt+' in the chosen direction while you hold <b>Measure</b> &mdash; keep the travel path clear.</p>'+'<p style=\"margin-top:8px\">Direction: <button type=\"button\" class=\"act-btn\" style=\"padding:6px 10px;font-size:12px\" onclick=\"orionCal.dir=-orionCal.dir;orionCalRender()\">'+dirTxt+'</button></p>';" \
      "btn.innerHTML='<button class=\"modal-cancel\" onclick=\"orionCalClose()\">Cancel</button><button class=\"modal-confirm\" onclick=\"orionCalGoto(1)\">Start</button>';" \
    "}else if(orionCal.step===1){" \
      "b.innerHTML='<p><b>Step 1 of 2 &mdash; No load.</b> Leave the cable slack / remove the fixture so the motor turns freely.</p>'+'<p style=\"margin-top:8px\">Free reading: <b>'+(orionCal.free!=null?orionCal.free:'\\u2014')+'</b></p>'+'<p id=\"orionCalLive\" style=\"color:var(--txt4)\"></p>'+orionCalMeasureBtn();" \
      "btn.innerHTML='<button class=\"modal-cancel\" onclick=\"orionCalClose()\">Cancel</button><button class=\"modal-confirm\" '+(orionCal.free!=null?'':'disabled')+' onclick=\"orionCalGoto(2)\">Next</button>';" \
    "}else if(orionCal.step===2){" \
      "b.innerHTML='<p><b>Step 2 of 2 &mdash; Under load.</b> Hang the fixture / take up the cable so the real load is applied.</p>'+'<p style=\"margin-top:8px\">Loaded reading: <b>'+(orionCal.loaded!=null?orionCal.loaded:'\\u2014')+'</b></p>'+'<p id=\"orionCalLive\" style=\"color:var(--txt4)\"></p>'+orionCalMeasureBtn();" \
      "btn.innerHTML='<button class=\"modal-cancel\" onclick=\"orionCalGoto(1)\">Back</button><button class=\"modal-confirm\" '+(orionCal.loaded!=null?'':'disabled')+' onclick=\"orionCalGoto(3)\">Finish</button>';" \
    "}else{" \
      "var t=orionCalThreshold();" \
      "b.innerHTML='<p>Free sg <b>'+orionCal.free+'</b>, loaded sg <b>'+orionCal.loaded+'</b>.</p>'+'<p style=\"margin-top:8px\">Stall threshold (SGTHRS) = <b>'+t+'</b> (midpoint).</p>'+(orionCal.free<=orionCal.loaded?'<p style=\"color:var(--red);margin-top:8px\">Warning: free reading is not higher than loaded &mdash; check sensor / load and redo.</p>':'');" \
      "btn.innerHTML='<button class=\"modal-cancel\" onclick=\"orionCalGoto(0)\">Redo</button><button class=\"modal-confirm\" onclick=\"orionCalSave()\">Save</button>';" \
    "}" \
    "var m=document.getElementById('orionCalMeasure');" \
    "if(m){var dn=function(e){e.preventDefault();orionCalSampleStart();};var up=function(e){e.preventDefault();orionCalSampleStop();};" \
      "m.addEventListener('mousedown',dn);m.addEventListener('mouseup',up);m.addEventListener('mouseleave',up);" \
      "m.addEventListener('touchstart',dn,{passive:false});m.addEventListener('touchend',up);m.addEventListener('touchcancel',up);}" \
  "}" \
  "function orionSetLimit(which){" \
    "if(!confirm('Set '+which+' limit to the current motor position?'))return;" \
    "var fd=new FormData();fd.append('which',which);" \
    "orionPost('/setlimit',fd).then(t=>{" \
      "var cm=parseFloat(t);if(isNaN(cm))return;" \
      "var fld=document.getElementById(which==='up'?'upCm':'downCm');" \
      "if(fld)fld.value=cm.toFixed(1);" \
      "if(typeof orionUApply==='function')orionUApply();" \
      "orionRefresh();" \
    "});" \
  "}" \
  "function orionRefresh(){" \
    "fetch('/motorstatus').then(r=>r.json()).then(d=>{" \
      "if(!d.available){" \
        "document.getElementById('orionState').textContent='unavailable';" \
        "document.getElementById('orionState').style.background='#c44';" \
        "return;" \
      "}" \
      "var st=document.getElementById('orionState');" \
      "st.textContent=d.state;st.style.background=orionStateColor(d.state);" \
      "document.getElementById('orionPosCm').textContent=(d.positionCm!=null)?d.positionCm.toFixed(1):'--';" \
      "document.getElementById('orionSg').textContent=d.sgResult;" \
      "var _sv=document.getElementById('orionSgthVal');if(_sv&&d.operSgthrs!=null)_sv.textContent=d.operSgthrs;" \
      "if(window.orionHomingSpeed==null&&d.homingSpeed!=null)window.orionHomingSpeed=d.homingSpeed;" \
      "document.getElementById('orionTemp').textContent=d.driverTemp||'--';" \
      "document.getElementById('orionHomed').textContent=d.homed?'homed':'not homed';" \
      "document.getElementById('orionHomed').style.color=d.homed?'var(--txt2)':'var(--txt3)';" \
      "var trv=document.getElementById('orionTravelCm');" \
      "if(trv&&d.downCm!=null&&d.upCm!=null){window.orionTravelBase=Math.abs(d.upCm-d.downCm);trv.textContent=(orionUnit==='in'?window.orionTravelBase/2.54:window.orionTravelBase).toFixed(1);}" \
      /* Gating: jog & setlimit only when DMX is not armed */ \
      "var dmxArmed=(d.dmxActive&&d.dmxEnable>0);" \
      "var override=!!d.override;" \
      "var jogU=document.getElementById('orionJogUp');" \
      "var jogD=document.getElementById('orionJogDown');" \
      "var rel =document.getElementById('orionReleaseBtn');" \
      "if(jogU){jogU.disabled=dmxArmed;}" \
      "if(jogD){jogD.disabled=dmxArmed;}" \
      "if(rel){rel.disabled=!override;}" \
      "document.querySelectorAll('[data-orion-needsdisarm]').forEach(b=>{b.disabled=dmxArmed;});" \
      "var f=[];" \
      "if(d.faultFlags&1)f.push('STALL');" \
      "if(d.faultFlags&2)f.push('OVERCURRENT');" \
      "if(d.faultFlags&4)f.push('OVERTEMP');" \
      "if(d.faultFlags&8)f.push('DRIVER_ERROR');" \
      "if(d.faultFlags&16)f.push('COMM_LOST');" \
      "if(d.faultFlags&32)f.push('NOT_HOMED');" \
      "document.getElementById('orionFaults').textContent=f.length?('faults: '+f.join(',')):'';" \
    "}).catch(()=>{});" \
  "}" \
  "function orionBindJog(id,dir){" \
    "var b=document.getElementById(id);if(!b)return;" \
    "var down=function(e){e.preventDefault();orionJogStart(dir);};" \
    "var up  =function(e){e.preventDefault();orionJogStop();};" \
    "b.addEventListener('mousedown',down);" \
    "b.addEventListener('mouseup',up);" \
    "b.addEventListener('mouseleave',up);" \
    "b.addEventListener('touchstart',down,{passive:false});" \
    "b.addEventListener('touchend',up);" \
    "b.addEventListener('touchcancel',up);" \
  "}" \
  /* cm <-> inch unit selector. The name='...Cm' fields stay canonical (always cm, */ \
  /* always submitted); inch entry goes through nameless proxy inputs.            */ \
  "var ORION_U=[{i:'jogSpeedCm',b:'Jog speed',k:'sp'},{i:'downCm',b:'DOWN limit',k:'d'},{i:'upCm',b:'UP limit',k:'d'},{i:'maxSpeedCm',b:'Max speed',k:'sp'},{i:'maxAccelCm',b:'Max accel',k:'ac'}];" \
  "var orionUnit=localStorage.getItem('orionUnit')||'cm';" \
  "function orionUSuf(k){var i=(orionUnit==='in');if(k==='d')return i?'in':'cm';if(k==='sp')return i?'in/s':'cm/s';return i?'in/s2':'cm/s2';}" \
  "function orionUApply(){" \
    "var inch=(orionUnit==='in');" \
    "ORION_U.forEach(function(o){" \
      "var f=document.getElementById(o.i),p=document.getElementById(o.i+'_in');" \
      "if(!f||!p)return;" \
      "if(inch){p.value=((parseFloat(f.value)||0)/2.54).toFixed(2);f.style.display='none';p.style.display='';}" \
      "else{f.style.display='';p.style.display='none';}" \
      "var lb=document.querySelector('label[for='+o.i+']');" \
      "if(lb)lb.textContent=o.b+' ('+orionUSuf(o.k)+')';" \
    "});" \
    "var rv=document.getElementById('orionResVal'),ru=document.getElementById('orionResU');" \
    "if(rv&&window.orionResBase)rv.textContent=(inch?window.orionResBase*2.54:window.orionResBase).toFixed(1);" \
    "if(ru)ru.textContent=inch?'steps/in':'steps/cm';" \
    "var tu=document.getElementById('orionTravelU');if(tu)tu.textContent=inch?'in':'cm';" \
    "var tv=document.getElementById('orionTravelCm');" \
    "if(tv&&window.orionTravelBase!=null)tv.textContent=(inch?window.orionTravelBase/2.54:window.orionTravelBase).toFixed(1);" \
    "if(typeof orionSums==='function')orionSums();" \
  "}" \
  "function orionUInit(){" \
    "var rb=document.getElementById('orionResVal');if(rb)window.orionResBase=parseFloat(rb.textContent)||0;" \
    "ORION_U.forEach(function(o){" \
      "var f=document.getElementById(o.i);if(!f)return;" \
      "var p=document.createElement('input');" \
      "p.type='number';p.step='0.01';p.id=o.i+'_in';" \
      "p.setAttribute('style',f.getAttribute('style')||'');" \
      "p.style.display='none';" \
      "f.parentNode.insertBefore(p,f.nextSibling);" \
      "p.addEventListener('input',function(){f.value=((parseFloat(p.value)||0)*2.54).toFixed(2);if(typeof orionSums==='function')orionSums();});" \
    "});" \
    "var s=document.getElementById('orionUnitSel');" \
    "if(s){s.value=orionUnit;s.addEventListener('change',function(){orionUnit=s.value;localStorage.setItem('orionUnit',orionUnit);orionUApply();});}" \
    "orionUApply();" \
  "}" \
  "orionUInit();" \
  "['personality','positionStart','controlStart','homingDirection','jogSpeedCm','downCm','upCm','maxSpeedCm','maxAccelCm','dmxWatchdogAction','drumDiaMm','gearRatio','motorStepsRev'].forEach(function(id){" \
    "var e=document.getElementById(id);if(e){e.addEventListener('input',orionSums);e.addEventListener('change',orionSums);}" \
  "});" \
  "for(var _i=0;document.getElementById('oLedProtoSel'+_i);_i++)orionLedSum(_i);" \
  "orionSums();" \
  "orionBindJog('orionJogUp','up');" \
  "orionBindJog('orionJogDown','down');" \
  "setInterval(orionRefresh,1000);orionRefresh();"

#endif // RAVLIGHT_FIXTURE_ORION
