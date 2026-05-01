#pragma once
#ifdef RAVLIGHT_FIXTURE_ELYON

// ── Elyon Outputs accordion ──────────────────────────────────────────────────
// Placeholders injected per output (i = 0..7):
//   {{elyon_gpio_i}}   — GPIO label (read-only)
//   {{elyon_proto_i}}  — selected="selected" per protocol option
//   {{elyon_count_i}}  — pixel_count value
//   {{elyon_univ_i}}   — universe_start value
//   {{elyon_ch_i}}     — dmx_start value
//   {{elyon_group_i}}  — grouping value
//   {{elyon_inv_i}}    — "checked" if invert
//   {{elyon_bri_i}}    — brightness value

#define ELYON_FIXTURE_HTML \
  "<div class=\"accordion\">" \
  "<button type=\"button\" class=\"accordion-button\">Elyon Outputs</button>" \
  "<div class=\"accordion-content\">" \
    "<p style=\"font-size:0.85em;color:#aaa;\">Auto-layout: " \
      "<label><input type=\"checkbox\" id=\"elyonAutoLayout\" checked " \
        "onchange=\"elyonAutoLayoutChanged()\"> Auto-calculate universes</label>" \
    "</p>" \
    "<div style=\"overflow-x:auto;\">" \
    "<table class=\"elyon-table\" style=\"border-collapse:collapse;font-size:0.85em;white-space:nowrap;\">" \
      "<thead><tr style=\"color:#aaa;\">" \
        "<th style=\"padding:4px 6px;text-align:left;\">Output</th>" \
        "<th style=\"padding:4px 4px;\">Type</th>" \
        "<th style=\"padding:4px 4px;\">Pixels</th>" \
        "<th style=\"padding:4px 4px;\">Universe</th>" \
        "<th style=\"padding:4px 4px;\">CH start</th>" \
        "<th style=\"padding:4px 4px;\">Group</th>" \
        "<th style=\"padding:4px 4px;\">Inv</th>" \
        "<th style=\"padding:4px 4px;\">Bri</th>" \
        "<th style=\"padding:4px 4px;\">Order</th>" \
      "</tr></thead><tbody>" \
    "{{ELYON_ROWS}}" \
    "</tbody></table></div>" \
    "<p id=\"elyonBudget\" style=\"font-size:0.85em;margin-top:6px;\"></p>" \
  "</div>" \
  "</div>"

// Single row template — N is substituted in C++ for each output
#define ELYON_ROW_TEMPLATE(N, GPIO, PROTO_WS2811, PROTO_WS2812B, PROTO_SK6812, \
                           COUNT, UNIV, CH, GROUP, INV, BRI) \
  "<tr>" \
    "<td style=\"padding:3px 6px;\">CH" #N " <span style=\"color:#666;\">(GPIO " GPIO ")</span></td>" \
    "<td><select name=\"elyonProto" #N "\" onchange=\"elyonRecalc()\">" \
      "<option value=\"0\"" PROTO_WS2811 ">WS2811</option>" \
      "<option value=\"1\"" PROTO_WS2812B ">WS2812B</option>" \
      "<option value=\"2\"" PROTO_SK6812 ">SK6812 (RGBW)</option>" \
    "</select></td>" \
    "<td><input type=\"number\" name=\"elyonCount" #N "\" value=\"" COUNT "\" " \
      "min=\"0\" max=\"4096\" style=\"width:60px;\" onchange=\"elyonRecalc()\"></td>" \
    "<td><input type=\"number\" name=\"elyonUniv" #N "\" id=\"elyonUniv" #N "\" value=\"" UNIV "\" " \
      "min=\"0\" max=\"32767\" style=\"width:60px;\"></td>" \
    "<td><input type=\"number\" name=\"elyonCh" #N "\" id=\"elyonCh" #N "\" value=\"" CH "\" " \
      "min=\"1\" max=\"512\" style=\"width:55px;\"></td>" \
    "<td><input type=\"number\" name=\"elyonGroup" #N "\" value=\"" GROUP "\" " \
      "min=\"1\" max=\"255\" style=\"width:45px;\" onchange=\"elyonRecalc()\"></td>" \
    "<td style=\"text-align:center;\"><input type=\"checkbox\" name=\"elyonInv" #N "\" " INV "></td>" \
    "<td><input type=\"number\" name=\"elyonBri" #N "\" value=\"" BRI "\" " \
      "min=\"0\" max=\"255\" style=\"width:50px;\"></td>" \
  "</tr>"

#define ELYON_FIXTURE_JS \
  "function elyonProtoChannels(proto){return(proto==2||proto==3)?4:3;}" \
  "function elyonRecalc(){" \
    "if(!document.getElementById('elyonAutoLayout').checked) return;" \
    "var univ=0,ch=1,total=0;" \
    "for(var i=0;i<8;i++){" \
      "var count=parseInt(document.querySelector('[name=elyonCount'+i+']').value)||0;" \
      "var proto=parseInt(document.querySelector('[name=elyonProto'+i+']').value)||1;" \
      "var group=parseInt(document.querySelector('[name=elyonGroup'+i+']').value)||1;" \
      "var univEl=document.getElementById('elyonUniv'+i);" \
      "var chEl=document.getElementById('elyonCh'+i);" \
      "total+=count;" \
      "if(count===0){if(univEl)univEl.value=univ;if(chEl)chEl.value=ch;continue;}" \
      "if(univEl)univEl.value=univ;" \
      "if(chEl)chEl.value=ch;" \
      "var slots=Math.ceil(count/group);" \
      "var dmxCh=slots*elyonProtoChannels(proto);" \
      "var flat=(ch-1)+dmxCh;" \
      "univ+=Math.floor(flat/512);" \
      "ch=(flat%512)+1;" \
    "}" \
    "var bud=document.getElementById('elyonBudget');" \
    "if(bud)bud.innerHTML='Total pixels: <b>'+total+'</b> / 4096" \
      "'+( total>4096?' <span style=\"color:red;\">⚠ Over budget</span>':'');" \
  "}" \
  "function elyonAutoLayoutChanged(){" \
    "var manual=!document.getElementById('elyonAutoLayout').checked;" \
    "for(var i=0;i<8;i++){" \
      "var u=document.getElementById('elyonUniv'+i);" \
      "var c=document.getElementById('elyonCh'+i);" \
      "if(u)u.readOnly=!manual;" \
      "if(c)c.readOnly=!manual;" \
    "}" \
    "if(!manual)elyonRecalc();" \
  "}" \
  "document.addEventListener('DOMContentLoaded',function(){" \
    "elyonAutoLayoutChanged();" \
    "elyonRecalc();" \
  "});"

#endif // RAVLIGHT_FIXTURE_ELYON
