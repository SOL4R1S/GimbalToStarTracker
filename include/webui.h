#pragma once
// 자동 생성 파일 — 수정 금지. 원본: web/index.html
// 재생성: python3 tools/gen_webui.py web/index.html include/webui.h
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AstroTrack</title><style>
:root{color-scheme:dark}
body{background:#0b0e14;color:#d7dde8;font-family:system-ui,sans-serif;margin:0;padding:1rem;max-width:480px;margin-inline:auto}
h1{font-size:1.2rem} fieldset{border:1px solid #2a3040;border-radius:.6rem;margin-bottom:1rem}
label{display:flex;justify-content:space-between;align-items:center;padding:.3rem 0;gap:.5rem}
input[type=number]{width:7rem;background:#151a24;color:#d7dde8;border:1px solid #2a3040;border-radius:.4rem;padding:.35rem}
button{background:#2563eb;color:#fff;border:0;border-radius:.5rem;padding:.7rem 1rem;font-size:1rem;width:100%;margin-top:.4rem}
button.stop{background:#b91c1c} button.test{background:#334155}
#status{font-family:ui-monospace,monospace;background:#151a24;border-radius:.6rem;padding:.7rem;line-height:1.5}
</style></head><body>
<h1>&#127760; AstroTrack</h1>
<div id="status">connecting...</div>
<fieldset><legend>&#9889; 시퀀스</legend>
<form id="f">
<label>시작 딜레이(s)<input name="delay" type="number" step="1" min="0"></label>
<label>노출(s)<input name="exposure" type="number" step="0.1" min="0.1"></label>
<label>갭(s)<input name="gap" type="number" step="0.1" min="0"></label>
<label>프레임 수<input name="frames" type="number" min="1"></label>
<label>추적<input name="tracking" type="checkbox" value="1" checked></label>
</fieldset>
<fieldset><legend>&#127786; 디더링</legend>
<label>N프레임마다(0=끔)<input name="ditherEvery" type="number" min="0"></label>
<label>진폭(&deg;)<input name="ditherAmp" type="number" step="0.1" min="0"></label>
<label>정착(s)<input name="settle" type="number" step="0.1" min="0.5"></label>
<button type="submit">&#128190; 저장</button>
</form></fieldset>
<button onclick="fetch('/start').then(st)">&#9654;&#65039; 시작</button>
<button class="stop" onclick="fetch('/stop').then(st)">&#11035;&#65039; 정지</button>
<button class="test" onclick="fetch('/testshot').then(st)">&#128248; 테스트컷(8s)</button>
<script>
const PH_KO={idle:'대기',delay:'대기 딜레이',opening:'셔터 열림',exposing:'노출중',
closing:'셔터 닫힘',dither:'디더링',settle:'정착',gap:'갭',done:'완료'};
function fmt(j){return `<b>${PH_KO[j.phase]??j.phase}</b> ${j.phase==='done'?'&#9989;':''}<br>`+
 `프레임 ${j.frame}/${j.frames}<br>트랙 ${j.trackSteps}스텝 · yaw ${j.yaw??'-'}&deg;<br>`+
 `<progress max="${j.frames}" value="${j.frame}" style="width:100%">`}
let prefilled=false;
async function st(){try{const j=await(await fetch('/status')).json();document.getElementById('status').innerHTML=fmt(j);
 const f=document.getElementById('f');
 if(!prefilled&&j.cfg)for(const[k,v]of Object.entries(j.cfg)){const el=f.elements[k];if(el&&el.type!=='checkbox')el.value=v}
 prefilled=true;
 }catch(e){document.getElementById('status').innerHTML='<span style="color:#f87171">&#128279; 연결 끊김 &mdash; AP 확인</span>'}}
document.getElementById('f').onsubmit=async e=>{e.preventDefault();
 const q=new URLSearchParams(new FormData(e.target));
 for(const[k,v]of[...q])if(v==='')q.delete(k);
 await fetch('/config',{method:'POST',body:q});
 st()};
setInterval(st,2000); st();
</script></body></html>)HTML";
