#pragma once
// Bootstrap page for the device-hosted setup portal (GET / on the portal
// server, main.cpp's "content portal" section).
//
// This ~1.7 KB document is the ONLY part of the portal UI that is frozen into
// the firmware image. Everything the user actually interacts with lives in
// the website's device-portal.js module (fetched from YAT_PORTAL_UI_BASE
// below), pulled in here by a dynamic import, so the portal can be reworked
// without reflashing a single device. The page's
// origin is the device, which is what makes the whole design work: an https
// page may not fetch http://<lan-ip> (mixed content + Private Network
// Access), but an http page served BY the device may fetch https assets, and
// its /api/* calls are same-origin — no CORS on the firmware side at all.
//
// UI base resolution order, most specific first:
//   1. "?ui=<base>" on the portal URL (also persisted, for repeat visits)
//   2. localStorage "yatUiBase"
//   3. YAT_PORTAL_UI_BASE, the compile-time default from platformio.ini
//
// If the import fails — no internet on the phone, website down, DNS blocked —
// the page falls back to a plain-HTML status/pack listing rendered from
// /api/status + /api/packs, so the device is never a blank page.

#ifndef YAT_PORTAL_UI_BASE
// Only reached if platformio.ini's build flag went missing; the fallback
// listing below still works, the rich UI just never loads.
#define YAT_PORTAL_UI_BASE "https://yat.hk"
#endif

static const char PORTAL_PAGE[] =
    R"HTML(<!doctype html>
<html lang="zh-Hant">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>YAT 設定 · Set up</title>
<style>
:root{color-scheme:light}
body{margin:0;padding:20px;background:#fafaf8;color:#111;
font:16px/1.55 -apple-system,BlinkMacSystemFont,"PingFang HK","Noto Sans TC","Helvetica Neue",sans-serif}
#app{max-width:34rem;margin:0 auto}
h1{font-size:1.25rem;margin:0 0 .4rem}
.muted{color:#666}
.err{background:#fff3cd;color:#664d03;padding:12px 14px;border-radius:12px}
a{color:#0b57d0}
</style>
</head>
<body>
<div id="app"><p>載入緊設定介面…<br><span class="muted">Loading the setup interface…</span></p></div>
<script type="module">
const app=document.getElementById("app"),KEY="yatUiBase";
const q=new URLSearchParams(location.search).get("ui");
let base=")HTML" YAT_PORTAL_UI_BASE R"HTML(";
try{if(q)localStorage.setItem(KEY,q);base=q||localStorage.getItem(KEY)||base}catch(e){base=q||base}
base=base.replace(/\/+$/,"");
const esc=s=>String(s).replace(/[&<>]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;"})[c]);
async function fallback(e){
console.error(e);
let st={},packs=[];
try{st=await(await fetch("/api/status")).json()}catch(x){}
try{packs=await(await fetch("/api/packs")).json()}catch(x){}
app.innerHTML='<h1>YAT</h1>'
+'<p class="err">介面載入唔到 — 檢查手機有冇上網<br>'
+'Couldn\'t load the interface — check your phone has internet</p>'
+'<p class="muted">fw '+esc(st.fw||"?")+' · '+esc(st.ip||"-")+' · '+esc(st.battery_mv||"?")+' mV · '
+esc(st.pages||0)+' 頁 pages · '+esc(st.page_id||"-")+'</p>'
+'<p>已安裝嘅版面 Installed packs:</p><ul>'
+(packs.map(p=>'<li>'+esc(p.id)+' ('+esc(p.size)+' B)</li>').join('')||'<li class="muted">—</li>')
+'</ul><p><a href="">再試一次 · Try again</a></p>';
}
const slow=new Promise((_,rej)=>setTimeout(()=>rej(new Error("ui load timeout")),15000));
Promise.race([import(base+"/device-portal.js"),slow]).then(m=>m.mount(app,{apiBase:""})).catch(fallback);
</script>
</body>
</html>
)HTML";
