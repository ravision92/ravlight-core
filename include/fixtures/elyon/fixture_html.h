#pragma once
#ifdef RAVLIGHT_FIXTURE_ELYON

// Expand HW_LED_OUTPUT_COUNT to a string literal for JS injection
#define _ELYON_STR(x) #x
#define _ELYON_XSTR(x) _ELYON_STR(x)

// ── Elyon Outputs accordion ──────────────────────────────────────────────────
// Rows injected via buildElyonRow() in webserver.cpp → {{ELYON_ROWS}}
// Two <tr> per output: row1 = config (Type/Pixels/Order), row2 = mapping (Univ/CH/Group/Bri)

#define ELYON_FIXTURE_HTML \
  "<div class=\"accordion\">" \
  "<button type=\"button\" class=\"accordion-button\">Elyon Outputs</button>" \
  "<div class=\"accordion-content\">" \
    "<div style=\"display:flex;align-items:center;gap:8px;margin-top:8px;margin-bottom:14px;\">" \
      "<input type=\"checkbox\" id=\"elyonAutoLayout\" onchange=\"elyonAutoLayoutChanged()\"" \
        " style=\"width:18px;height:18px;flex-shrink:0;margin:0;\">" \
      "<label for=\"elyonAutoLayout\"" \
        " style=\"display:inline;width:auto;margin:0;font-size:0.9em;color:#ccc;\">Auto-calculate universes &amp; channels</label>" \
    "</div>" \
    "<div style=\"overflow-x:auto;\">" \
    "<table style=\"border-collapse:collapse;font-size:0.9em;white-space:nowrap;\">" \
      "<thead><tr style=\"color:#666;font-size:0.8em;\">" \
        "<th style=\"padding:4px 8px;text-align:left;\">Output</th>" \
        "<th style=\"padding:4px 6px;\">Type</th>" \
        "<th style=\"padding:4px 6px;\">Pixels</th>" \
        "<th style=\"padding:4px 6px;\">Group</th>" \
      "</tr></thead><tbody>" \
    "{{ELYON_ROWS}}" \
    "</tbody></table>" \
    "</div>" \
    "<p id=\"elyonBudget\" style=\"font-size:0.85em;margin-top:8px;\"></p>" \
  "</div>" \
  "</div>"

#define ELYON_FIXTURE_JS \
  "const ELYON_OUT_COUNT=" _ELYON_XSTR(HW_LED_OUTPUT_COUNT) ";" \
  "function elyonProtoChannels(proto){return(proto==2||proto==3)?4:3;}" \
  "function elyonRecalc(){" \
    "var auto=document.getElementById('elyonAutoLayout').checked;" \
    "var univ=0,ch=1,total=0;" \
    "for(var i=0;i<ELYON_OUT_COUNT;i++){" \
      "var count=parseInt(document.querySelector('[name=elyonCount'+i+']').value)||0;" \
      "var proto=parseInt(document.querySelector('[name=elyonProto'+i+']').value)||1;" \
      "var group=parseInt(document.querySelector('[name=elyonGroup'+i+']').value)||1;" \
      "var univEl=document.getElementById('elyonUniv'+i);" \
      "var chEl=document.getElementById('elyonCh'+i);" \
      "total+=count;" \
      "if(count===0){if(auto){if(univEl)univEl.value=univ;if(chEl)chEl.value=ch;}continue;}" \
      "if(auto){if(univEl)univEl.value=univ;if(chEl)chEl.value=ch;}" \
      "var slots=Math.ceil(count/group);" \
      "var dmxCh=slots*elyonProtoChannels(proto);" \
      "var flat=(ch-1)+dmxCh;" \
      "univ+=Math.floor(flat/512);" \
      "ch=(flat%512)+1;" \
    "}" \
    "var bud=document.getElementById('elyonBudget');" \
    "if(bud)bud.innerHTML='Total pixels: <b>'+total+'</b> / 4096'" \
      "+(total>4096?' <span style=\"color:red;\">&#9888; Over budget</span>':'');" \
  "}" \
  "function elyonAutoLayoutChanged(){" \
    "var auto=document.getElementById('elyonAutoLayout').checked;" \
    "for(var i=0;i<ELYON_OUT_COUNT;i++){" \
      "var u=document.getElementById('elyonUniv'+i);" \
      "var c=document.getElementById('elyonCh'+i);" \
      "if(u)u.readOnly=auto;" \
      "if(c)c.readOnly=auto;" \
    "}" \
    "elyonRecalc();" \
  "}" \
  "document.addEventListener('DOMContentLoaded',function(){" \
    "elyonAutoLayoutChanged();" \
  "});"

#endif // RAVLIGHT_FIXTURE_ELYON
