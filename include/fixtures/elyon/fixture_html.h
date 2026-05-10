#pragma once
#ifdef RAVLIGHT_FIXTURE_ELYON

// Expand HW_LED_OUTPUT_COUNT to a string literal for JS injection
#define _ELYON_STR(x) #x
#define _ELYON_XSTR(x) _ELYON_STR(x)

// ── Elyon Outputs accordion ───────────────────────────────────────────────────
// Cards are generated server-side by buildElyonCard() in webserver.cpp → {{ELYON_ROWS}}

#define ELYON_FIXTURE_HTML \
  "<div class=\"acc-wrap\">" \
  "<button type=\"button\" class=\"acc-btn open\" onclick=\"toggleAcc(this)\">" \
    "<span>Elyon Outputs " \
      "<span style=\"font-size:11px;color:var(--txt3);font-weight:400\">" \
        "&middot; " _ELYON_XSTR(HW_LED_OUTPUT_COUNT) " CH" \
      "</span>" \
    "</span>" \
    "<span class=\"acc-arrow\">&#9661;</span>" \
  "</button>" \
  "<div class=\"acc-body open\">" \
    "<div style=\"padding:12px 12px 4px\">" \
      "<div class=\"tog-row\">" \
        "<input type=\"checkbox\" id=\"elyonAutoLayout\" onchange=\"elyonAutoLayoutChanged()\">" \
        "<span class=\"tog-lbl\">Auto-calculate universes &amp; channels</span>" \
      "</div>" \
    "</div>" \
    "<div class=\"ch-list\">" \
    "{{ELYON_ROWS}}" \
    "</div>" \
    "<div class=\"budget\">" \
      "<span class=\"bud-lbl\">Pixels</span>" \
      "<div class=\"bud-bar\"><div class=\"bud-fill\" id=\"budFill\"></div></div>" \
      "<span class=\"bud-val\" id=\"budVal\">0 / 4096</span>" \
    "</div>" \
    "<div class=\"timer-warn\" id=\"timerWarn\"></div>" \
    "<div style=\"height:10px\"></div>" \
  "</div>" \
  "</div>"

#define ELYON_FIXTURE_JS \
  "const ELYON_OUT_COUNT=" _ELYON_XSTR(HW_LED_OUTPUT_COUNT) ";" \
  "function toggleCh(id){" \
    "var c=document.getElementById('card-'+id);" \
    "if(!c)return;" \
    "var body=c.querySelector('.ch-body');" \
    "if(!body)return;" \
    "var opening=!c.classList.contains('open');" \
    "c.classList.toggle('open');" \
    "if(opening){" \
      "body.style.maxHeight=body.scrollHeight+'px';" \
      "body.addEventListener('transitionend',function h(){" \
        "body.style.maxHeight='none';" \
        "body.removeEventListener('transitionend',h);" \
      "},{once:true});" \
    "}else{" \
      "body.style.maxHeight=body.scrollHeight+'px';" \
      "requestAnimationFrame(function(){requestAnimationFrame(function(){" \
        "body.style.maxHeight='0';" \
      "});});" \
    "}" \
  "}" \
  "function protoChange(id){" \
    "var p=document.getElementById('proto'+id);" \
    "if(!p)return;" \
    "var pwm=(p.value==='50');" \
    "var ps=document.getElementById('pwmSec'+id);" \
    "var xs=document.getElementById('pxSec'+id);" \
    "if(ps)ps.style.display=pwm?'':'none';" \
    "if(xs)xs.style.display=pwm?'none':'';" \
    "sumUpdate(id);" \
    "elyonRecalc();" \
  "}" \
  "function sumUpdate(id){" \
    "var p=document.getElementById('proto'+id);" \
    "if(!p)return;" \
    "var pwm=(p.value==='50');" \
    "var PNAMES=['WS2811','WS2812B','SK6812 RGBW','WS2814 RGBW'];" \
    "var sp=document.getElementById('sProto'+id);" \
    "if(sp)sp.textContent=pwm?'PWM Dimmer':(PNAMES[parseInt(p.value)]||p.value);" \
    "var s1=document.getElementById('sP1'+id);" \
    "if(s1){" \
      "if(pwm){" \
        "var fv=(document.getElementById('freq'+id)||{value:'1000'}).value;" \
        "var fm={'100':'100Hz','500':'500Hz','1000':'1kHz','5000':'5kHz','10000':'10kHz','20000':'20kHz'};" \
        "s1.innerHTML='<b>'+(fm[fv]||fv)+'</b>';" \
      "}else{" \
        "s1.innerHTML='<b>'+((document.getElementById('count_'+id)||{value:'0'}).value)+'</b> px';" \
      "}" \
    "}" \
    "var su=document.getElementById('sU'+id);" \
    "var sc=document.getElementById('sCH'+id);" \
    "if(su)su.textContent=(document.getElementById('univ_'+id)||{value:'0'}).value;" \
    "if(sc)sc.textContent=(document.getElementById('sch_'+id)||{value:'1'}).value;" \
  "}" \
  "function elyonProtoChannels(proto){" \
    "if(proto===50)return 0;" \
    "return(proto===2||proto===3)?4:3;" \
  "}" \
  "function elyonRecalc(){" \
    "var autoEl=document.getElementById('elyonAutoLayout');" \
    "var auto=autoEl?autoEl.checked:false;" \
    "var univ=0,ch=1,total=0,warn='';" \
    "var timerFreqs={};" \
    "for(var i=0;i<ELYON_OUT_COUNT;i++){" \
      "var proto=parseInt((document.getElementById('proto'+i)||{value:'1'}).value)||1;" \
      "var univEl=document.getElementById('univ_'+i);" \
      "var chEl=document.getElementById('sch_'+i);" \
      "if(proto===50){" \
        "var frqEl=document.getElementById('freq'+i);" \
        "var freq=frqEl?parseInt(frqEl.value)||0:0;" \
        "var bitEl=document.getElementById('bit16_'+i);" \
        "var is16=bitEl&&bitEl.checked?1:0;" \
        "var dmxCh=freq>0?(is16?2:1):0;" \
        "if(dmxCh===0){if(auto){if(univEl)univEl.value=univ;if(chEl)chEl.value=ch;}continue;}" \
        "if(auto){if(univEl)univEl.value=univ;if(chEl)chEl.value=ch;}" \
        "var timer=i%4;" \
        "if(timerFreqs[timer]!==undefined&&timerFreqs[timer]!==freq)warn+='CH'+(i+1)+' ';" \
        "else timerFreqs[timer]=freq;" \
        "var flat=(ch-1)+dmxCh;univ+=Math.floor(flat/512);ch=(flat%512)+1;" \
      "}else{" \
        "var cnt=parseInt((document.getElementById('count_'+i)||{value:'0'}).value)||0;" \
        "var grp=parseInt((document.getElementById('grp_'+i)||{value:'1'}).value)||1;" \
        "total+=cnt;" \
        "if(cnt===0){if(auto){if(univEl)univEl.value=univ;if(chEl)chEl.value=ch;}continue;}" \
        "if(auto){if(univEl)univEl.value=univ;if(chEl)chEl.value=ch;}" \
        "var slots=Math.ceil(cnt/grp);" \
        "var dmxCh2=slots*elyonProtoChannels(proto);" \
        "var flat2=(ch-1)+dmxCh2;univ+=Math.floor(flat2/512);ch=(flat2%512)+1;" \
      "}" \
      "sumUpdate(i);" \
    "}" \
    "var pct=Math.min(100,(total/4096)*100);" \
    "var fill=document.getElementById('budFill');" \
    "if(fill){fill.style.width=pct+'%';fill.style.background=pct>90?'#ff5533':pct>70?'#ff9900':'#e9ff00';}" \
    "var bv=document.getElementById('budVal');" \
    "if(bv)bv.textContent=total+' / 4096'+(total>4096?' ⚠':'');" \
    "var tw=document.getElementById('timerWarn');" \
    "if(tw){tw.style.display=warn?'block':'none';" \
      "tw.textContent=warn?'⚠ Timer conflict: '+warn+'\\u2014 same frequency required per timer group':'';" \
    "}" \
  "}" \
  "function elyonAutoLayoutChanged(){" \
    "var auto=(document.getElementById('elyonAutoLayout')||{checked:false}).checked;" \
    "for(var i=0;i<ELYON_OUT_COUNT;i++){" \
      "var u=document.getElementById('univ_'+i);" \
      "var c=document.getElementById('sch_'+i);" \
      "if(u){u.readOnly=auto;u.style.opacity=auto?'.4':'1';}" \
      "if(c){c.readOnly=auto;c.style.opacity=auto?'.4':'1';}" \
    "}" \
    "elyonRecalc();" \
  "}" \
  "document.addEventListener('DOMContentLoaded',function(){" \
    "elyonAutoLayoutChanged();" \
    "elyonRecalc();" \
  "});"

#endif // RAVLIGHT_FIXTURE_ELYON
