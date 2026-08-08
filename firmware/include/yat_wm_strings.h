/**
 * yat_wm_strings.h — YAT's replacement for WiFiManager's wm_strings_en.h.
 *
 * Selected by the -DWM_STRINGS_FILE='"yat_wm_strings.h"' build flag in
 * platformio.ini, which is the library's own supported override hook:
 * WiFiManager.h does `#include WM_STRINGS_FILE` and never touches
 * wm_strings_en.h when that define is set. Every PROGMEM symbol the library
 * references therefore has to be defined here — this file started as a verbatim
 * clone of wm_strings_en.h (v2.0.17) and only the user-visible parts changed.
 *
 * Two reasons this is the whole-file clone rather than setCustomHeadElement
 * patches: the post-save page (HTTP_SAVED) and the connect-failure lines
 * (HTTP_STATUS_OFF*) exist nowhere else — there is no setter for them — and the
 * portal is served offline from the device's own AP, so every byte of styling
 * has to be inline anyway.
 *
 * House style for anything a user reads: 廣東話 first, English under it, and
 * no jargon the box does not also use. The device is "部機", the panel is "個芒".
 */

#ifndef _YAT_WM_STRINGS_H_
#define _YAT_WM_STRINGS_H_

// strings files must include a consts file (tokens, routes, HTTP_HEAD_CT)
#include "wm_consts_en.h"

const char WM_LANGUAGE[] PROGMEM = "zh-HK";

// theme-color paints the phone's browser chrome the same paper as the page;
// format-detection stops iOS turning SSIDs that look like numbers into
// tappable phone links.
const char HTTP_HEAD_START[]       PROGMEM = "<!DOCTYPE html>"
"<html lang='zh-HK'><head>"
"<meta name='format-detection' content='telephone=no'>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'/>"
"<meta name='theme-color' content='#fbf9f5'>"
"<title>{v} · YAT 日</title>";

// Unchanged from stock: c() fills the SSID box from a tapped scan row and
// enables the password box only for encrypted networks (it reads the 'l' class
// off the <a>'s nextElementSibling, so HTTP_ITEM must keep {qi} directly after
// the link). f() is the show-password toggle.
const char HTTP_SCRIPT[]           PROGMEM = "<script>function c(l){"
"document.getElementById('s').value=l.getAttribute('data-ssid')||l.innerText||l.textContent;"
"p = l.nextElementSibling.classList.contains('l');"
"document.getElementById('p').disabled = !p;"
"if(p)document.getElementById('p').focus();};"
"function f() {var x = document.getElementById('p');x.type==='password'?x.type='text':x.type='password';}"
"</script>";

// The masthead is markup rather than CSS so it is the first thing a captive
// portal mini-browser paints, even if it drops a stylesheet rule or two.
// {c} = _bodyclass (main.cpp sets it to "yat").
const char HTTP_HEAD_END[]         PROGMEM = "</head><body class='{c}'>"
"<div class='bar'><span class='br'>YAT <b>日</b></span>"
"<span class='bs'>設定 Wi-Fi · Wi-Fi setup</span></div>"
"<div class='wrap'>";

// Root page. {t} = setTitle(), {v} = the AP name while the portal is up. The
// masthead already carries the wordmark, so this says what to do instead of
// repeating the brand.
const char HTTP_ROOT_MAIN[]        PROGMEM = "<h1>撳掣揀你屋企個 Wi-Fi<span>Tap below to pick your home Wi-Fi</span></h1>"
"<p class='sub'>手機而家連住 {v}<br>Your phone is on {v}</p>";

const char * const HTTP_PORTAL_MENU[] PROGMEM = {
"<form action='/wifi'    method='get'><button>揀 Wi-Fi 同入密碼<span>Choose Wi-Fi &amp; enter password</span></button></form>\n", // MENU_WIFI
"<form action='/0wifi'   method='get'><button class='sec'>自己打 Wi-Fi 名<span>Type the network name myself</span></button></form>\n", // MENU_WIFINOSCAN
"<form action='/info'    method='get'><button class='sec'>裝置資料<span>Device info</span></button></form>\n", // MENU_INFO
"<form action='/param'   method='get'><button class='sec'>設定<span>Settings</span></button></form>\n",//MENU_PARAM
"<form action='/close'   method='get'><button class='sec'>閂咗呢頁<span>Close this page</span></button></form>\n", // MENU_CLOSE
"<form action='/restart' method='get'><button class='sec'>重新開機<span>Restart</span></button></form>\n",// MENU_RESTART
"<form action='/exit'    method='get'><button class='sec'>離開設定<span>Exit setup</span></button></form>\n",  // MENU_EXIT
"<form action='/erase'   method='get'><button class='D'>清除 Wi-Fi 記錄<span>Erase saved Wi-Fi</span></button></form>\n", // MENU_ERASE
"<form action='/update'  method='get'><button class='sec'>更新韌體<span>Update firmware</span></button></form>\n",// MENU_UPDATE
"<hr>" // MENU_SEP
};

const char HTTP_PORTAL_OPTIONS[]   PROGMEM = "";
const char HTTP_ITEM_QI[]          PROGMEM = "<div role='img' aria-label='{r}%' title='{r}%' class='q q-{q} {i} {h}'></div>"; // rssi icons
const char HTTP_ITEM_QP[]          PROGMEM = "<div class='q {h}'>{r}%</div>"; // rssi percentage {h} = hidden showperc pref
// class='ap' is the only structural change: it makes each scan result a full
// -width touch row instead of a bare div. {qi} must stay directly after </a>
// — HTTP_SCRIPT's c() reads the lock class off it.
const char HTTP_ITEM[]             PROGMEM = "<div class='ap'><a href='#p' onclick='c(this)' data-ssid='{V}'>{v}</a>{qi}{qp}</div>";

const char HTTP_FORM_START[]       PROGMEM = "<form method='POST' action='{v}'>";
// G4 lives in the label itself: the 2.4 GHz rule matters exactly where the
// network gets picked, not as a banner the user has already scrolled past.
const char HTTP_FORM_WIFI[]        PROGMEM = "<label for='s'>Wi-Fi 名（只支援 2.4GHz）<span>Network name (2.4 GHz only)</span></label>"
"<input id='s' name='s' maxlength='32' autocomplete='off' autocorrect='off' autocapitalize='none' spellcheck='false' placeholder='{v}'>"
"<label for='p'>Wi-Fi 密碼<span>Wi-Fi password</span></label>"
// autocomplete='off' on purpose: this is a router password, and letting a
// phone's password manager offer to save it "for 192.168.4.1" is noise the
// user then has to dismiss mid-setup
"<input id='p' name='p' maxlength='64' type='password' autocomplete='off' autocorrect='off' autocapitalize='none' spellcheck='false' placeholder='{p}'>"
"<label class='ck'><input type='checkbox' id='showpass' onclick='f()'> 顯示密碼 Show password</label>";
const char HTTP_FORM_WIFI_END[]    PROGMEM = "";
const char HTTP_FORM_STATIC_HEAD[] PROGMEM = "<hr>";
const char HTTP_FORM_END[]         PROGMEM = "<button type='submit'>儲存並連線<span>Save &amp; connect</span></button></form>";
const char HTTP_FORM_LABEL[]       PROGMEM = "<label for='{i}'>{t}</label>";
const char HTTP_FORM_PARAM_HEAD[]  PROGMEM = "<hr>";
const char HTTP_FORM_PARAM[]       PROGMEM = "<input id='{i}' name='{n}' maxlength='{l}' value='{v}' {c}>\n"; // do not remove newline!

const char HTTP_SCAN_LINK[]        PROGMEM = "<form action='/wifi?refresh=1' method='POST'><button class='sec' name='refresh' value='1'>搵唔到？再掃一次<span>Not listed? Scan again</span></button></form>";

// THE page that was missing. The portal's AP disappears moments after this is
// sent (the device drops the AP, joins the home network and starts a ~30 s
// panel refresh), so this text is the last thing the phone will show — it has
// to explain the silence that follows rather than leave the user waiting on a
// dead page.
const char HTTP_SAVED[]            PROGMEM = "<div class='msg S'>"
"<strong>儲存咗！ Saved!</strong>"
"<p>你可以閂咗呢頁喇。部機而家會自己重新畫個芒（大概一分鐘），畫完會出一個新嘅 QR code 教你揀內容。手機記得連返屋企個 Wi-Fi。</p>"
"<p class='en'>You can close this page. The device now redraws its screen by itself — about a minute — and will show a new QR code for choosing content. Reconnect your phone to your home Wi-Fi.</p>"
"</div>";
const char HTTP_PARAMSAVED[]       PROGMEM = "<div class='msg S'><strong>儲存咗 Saved</strong></div>";
const char HTTP_END[]              PROGMEM = "</div></body></html>";
const char HTTP_ERASEBTN[]         PROGMEM = "<form action='/erase' method='get'><button class='D'>清除 Wi-Fi 記錄<span>Erase saved Wi-Fi</span></button></form>";
const char HTTP_UPDATEBTN[]        PROGMEM = "<form action='/update' method='get'><button class='sec'>更新韌體<span>Update firmware</span></button></form>";
const char HTTP_BACKBTN[]          PROGMEM = "<hr><form action='/' method='get'><button class='sec'>返去<span>Back</span></button></form>";

const char HTTP_STATUS_ON[]        PROGMEM = "<div class='msg S'><strong>連咗 Connected</strong><p>{v}</p><p class='en'>IP {i}</p></div>";
const char HTTP_STATUS_OFF[]       PROGMEM = "<div class='msg {c}'><strong>仲未連到 Not connected</strong><p>{v}{r}</p></div>"; // {c=class} {v=ssid} {r=status_off}
const char HTTP_STATUS_OFFPW[]     PROGMEM = "<br/>密碼唔啱，請再入過一次。<br/>Wrong password — please type it again."; // STATION_WRONG_PASSWORD, no esp32
const char HTTP_STATUS_OFFNOAP[]   PROGMEM = "<br/>搵唔到呢個 Wi-Fi。記住 YAT 只連 2.4GHz。<br/>Network not found. YAT only joins 2.4 GHz networks.";   // WL_NO_SSID_AVAIL
const char HTTP_STATUS_OFFFAIL[]   PROGMEM = "<br/>連唔到，請再試一次。<br/>Could not connect — please try again."; // WL_CONNECT_FAILED
const char HTTP_STATUS_NONE[]      PROGMEM = "<div class='msg'>仲未揀 Wi-Fi · No Wi-Fi chosen yet</div>";
const char HTTP_BR[]               PROGMEM = "<br/>";

// Paper #fbf9f5 and red #b8402c are the product's own colours (site + panel
// chrome). PingFang ahead of the Latin faces so 廣東話 renders as the phone's
// own system Chinese rather than a fallback. Everything inline: the phone has
// no internet while it is on the device's AP.
const char HTTP_STYLE[]            PROGMEM = "<style>"
":root{--paper:#fbf9f5;--ink:#1c1a17;--red:#b8402c;--mut:#6f685c;--line:#e4dfd4;--card:#fff}"
"*{box-sizing:border-box}"
"html{-webkit-text-size-adjust:100%}"
"body{margin:0;background:var(--paper);color:var(--ink);font-size:17px;line-height:1.5;"
"font-family:-apple-system,BlinkMacSystemFont,'PingFang HK','PingFang TC','Noto Sans HK','Hiragino Sans','Microsoft JhengHei','Helvetica Neue',Arial,sans-serif}"
// masthead
".bar{display:flex;align-items:baseline;flex-wrap:wrap;gap:10px;padding:14px 18px;"
"background:var(--paper);border-bottom:2px solid var(--ink)}"
".br{font-size:1.2rem;font-weight:800;color:var(--red);letter-spacing:.01em}"
".br b{font-weight:800}"
".bs{font-size:.85rem;color:var(--mut)}"
// flex column purely so .msg.D can be pulled to the top — see below
".wrap{display:flex;flex-direction:column;max-width:460px;margin:0 auto;padding:20px 18px 4px;text-align:left}"
// headings
"h1{font-size:1.4rem;line-height:1.35;margin:0 0 .35em}"
"h1 span,h2 span{display:block;font-size:.92rem;font-weight:500;color:var(--mut);margin-top:.15em}"
"h2{font-size:1.15rem;margin:1.2em 0 .4em}"
"h3{font-size:.95rem;font-weight:500;color:var(--mut);margin:0 0 1em}"
".sub{color:var(--mut);font-size:.9rem;margin:0 0 1.3em}"
"hr{border:0;border-top:1px solid var(--line);margin:22px 0}"
"p{margin:.6em 0}"
".en{color:var(--mut);font-size:.92rem}"
// buttons — 56px min so a thumb cannot miss, sub-label in a lighter weight
"form{margin:0 0 12px}"
"button{-webkit-appearance:none;appearance:none;display:block;width:100%;min-height:56px;"
"padding:11px 16px;border:0;border-radius:14px;background:var(--red);color:#fff;"
"font-family:inherit;font-size:1.05rem;font-weight:700;line-height:1.3;cursor:pointer;text-align:center}"
"button span{display:block;font-size:.82rem;font-weight:500;opacity:.9;margin-top:2px}"
"button:active{opacity:.7}"
"button.sec{background:transparent;color:var(--ink);border:2px solid var(--line);font-weight:600}"
"button.sec span{color:var(--mut);opacity:1}"
"button.D{background:#8c2f1f}"
// fields
"label{display:block;font-size:.98rem;font-weight:700;margin:18px 0 7px}"
"label span{display:block;font-size:.85rem;font-weight:500;color:var(--mut)}"
"input{width:100%;min-height:54px;padding:12px 14px;border:2px solid var(--line);border-radius:12px;"
"background:var(--card);color:var(--ink);font-family:inherit;font-size:1.05rem}"
"input:focus{outline:none;border-color:var(--red)}"
"input::placeholder{color:#b3ab9d}"
"label.ck{display:flex;align-items:center;gap:10px;font-weight:500;font-size:.95rem;"
"color:var(--mut);margin:14px 0 22px}"
"input[type=checkbox]{width:24px;min-width:24px;height:24px;min-height:24px;margin:0;padding:0;"
"accent-color:var(--red)}"
":disabled{opacity:.5}"
// Scan results. The row is position:relative rather than flex so its ::before
// can be a block — that pseudo-element is the only place a "these are tappable"
// instruction can go: the library emits the scan list immediately after
// HTTP_HEAD_END with no container and no hook of its own, and HTTP_HEAD_END is
// shared with every other page. :first-child scopes it to the scan page, since
// nowhere else does a .ap open the wrap.
".ap{position:relative;border-bottom:1px solid var(--line)}"
".ap a{display:block;padding:17px 44px 17px 2px;color:var(--ink);font-weight:600;"
"text-decoration:none;word-break:break-word}"
".ap a:active{color:var(--red)}"
".wrap>.ap:first-child::before{content:'撳你屋企個 Wi-Fi 名 · Tap your home network';"
"display:block;padding:0 0 12px;font-size:.92rem;font-weight:500;color:var(--mut)}"
"a{color:var(--ink)}"
// signal-strength sprite, inline base64, unchanged from stock (float dropped —
// the row is flex now); .l on the icon is also what the SSID-pick script reads
// bottom-anchored, not centred: the first row is taller than the rest (it
// carries the ::before instruction) and a long SSID wraps, so measuring from
// the row's last line is the only alignment that holds in both cases
".q{position:absolute;right:2px;bottom:21px;height:16px;margin:0;padding:0;"
"text-align:right;min-width:22px;color:var(--mut);font-size:.8rem}"
".q.q-0:after{background-position-x:0}.q.q-1:after{background-position-x:-16px}.q.q-2:after{background-position-x:-32px}.q.q-3:after{background-position-x:-48px}.q.q-4:after{background-position-x:-64px}"
".q.l:before{background-position-x:-80px;padding-right:5px}"
".q:after,.q:before{content:'';width:16px;height:16px;display:inline-block;background-repeat:no-repeat;background-position: 16px 0;"
"background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAGAAAAAQCAMAAADeZIrLAAAAJFBMVEX///8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADHJj5lAAAAC3RSTlMAIjN3iJmqu8zd7vF8pzcAAABsSURBVHja7Y1BCsAwCASNSVo3/v+/BUEiXnIoXkoX5jAQMxTHzK9cVSnvDxwD8bFx8PhZ9q8FmghXBhqA1faxk92PsxvRc2CCCFdhQCbRkLoAQ3q/wWUBqG35ZxtVzW4Ed6LngPyBU2CobdIDQ5oPWI5nCUwAAAAASUVORK5CYII=');}"
"@media (-webkit-min-device-pixel-ratio: 2),(min-resolution: 192dpi){.q:before,.q:after {"
"background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAALwAAAAgCAMAAACfM+KhAAAALVBMVEX///8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADAOrOgAAAADnRSTlMAESIzRGZ3iJmqu8zd7gKjCLQAAACmSURBVHgB7dDBCoMwEEXRmKlVY3L//3NLhyzqIqSUggy8uxnhCR5Mo8xLt+14aZ7wwgsvvPA/ofv9+44334UXXngvb6XsFhO/VoC2RsSv9J7x8BnYLW+AjT56ud/uePMdb7IP8Bsc/e7h8Cfk912ghsNXWPpDC4hvN+D1560A1QPORyh84VKLjjdvfPFm++i9EWq0348XXnjhhT+4dIbCW+WjZim9AKk4UZMnnCEuAAAAAElFTkSuQmCC');"
"background-size: 95px 16px;}}"
".h{display:none}"
// status / result cards
".msg{padding:16px 18px;margin:20px 0;background:var(--card);border:1px solid var(--line);"
"border-left:6px solid var(--mut);border-radius:14px}"
".msg strong{display:block;font-size:1.1rem;margin-bottom:.3em}"
".msg p{margin:.5em 0 0}"
".msg.P{border-left-color:var(--red)}"
// A failure card is appended last by reportStatus(), which on the scan page
// puts "wrong password" below the fold under the Save button. order:-1 (the
// reason .wrap is a flex column) lifts only the failure variants to the top;
// the neutral "no Wi-Fi chosen yet" and the success card stay where they are.
".msg.D{order:-1;border-left-color:var(--red);background:#fdf3f1}"
".msg.S{border-left-color:#2f7d4f;background:#f3f8f4}"
// info page leftovers (not linked from the menu, kept legible anyway)
"dt{font-weight:700}dd{margin:0;padding:0 0 .5em 0;min-height:12px}"
"td{vertical-align:top}"
"body.invert,body.invert a,body.invert h1{background-color:#141210;color:#f4f1ea}"
"body.invert .msg{color:#f4f1ea;background-color:#221f1b;border-color:#3a352e}"
"body.invert .q[role=img]{-webkit-filter:invert(1);filter:invert(1)}"
"</style>";

#ifndef WM_NOHELP
const char HTTP_HELP[]             PROGMEM =
 "<br/><h3>Available pages</h3><hr>"
 "<table class='table'>"
 "<thead><tr><th>Page</th><th>Function</th></tr></thead><tbody>"
 "<tr><td><a href='/'>/</a></td>"
 "<td>Menu page.</td></tr>"
 "<tr><td><a href='/wifi'>/wifi</a></td>"
 "<td>Show WiFi scan results and enter WiFi configuration.(/0wifi noscan)</td></tr>"
 "<tr><td><a href='/wifisave'>/wifisave</a></td>"
 "<td>Save WiFi configuration information and configure device. Needs variables supplied.</td></tr>"
 "<tr><td><a href='/param'>/param</a></td>"
 "<td>Parameter page</td></tr>"
 "<tr><td><a href='/info'>/info</a></td>"
 "<td>Information page</td></tr>"
 "<tr><td><a href='/u'>/u</a></td>"
 "<td>OTA Update</td></tr>"
 "<tr><td><a href='/close'>/close</a></td>"
 "<td>Close the captiveportal popup, config portal will remain active</td></tr>"
 "<tr><td>/exit</td>"
 "<td>Exit Config portal, config portal will close</td></tr>"
 "<tr><td>/restart</td>"
 "<td>Reboot the device</td></tr>"
 "<tr><td>/erase</td>"
 "<td>Erase WiFi configuration and reboot device. Device will not reconnect to a network until new WiFi configuration data is entered.</td></tr>"
 "</table>"
 "<p/>Github <a href='https://github.com/tzapu/WiFiManager'>https://github.com/tzapu/WiFiManager</a>.";
#else
const char HTTP_HELP[]             PROGMEM = "";
#endif

const char HTTP_UPDATE[] PROGMEM = "Upload new firmware<br/><form method='POST' action='u' enctype='multipart/form-data' onchange=\"(function(el){document.getElementById('uploadbin').style.display = el.value=='' ? 'none' : 'initial';})(this)\"><input type='file' name='update' accept='.bin,application/octet-stream'><button id='uploadbin' type='submit' class='h D'>Update</button></form><small><a href='http://192.168.4.1/update' target='_blank'>* May not function inside captive portal, open in browser http://192.168.4.1</a><small>";
const char HTTP_UPDATE_FAIL[] PROGMEM = "<div class='msg D'><strong>更新失敗 Update failed</strong>請重新開機再試一次。<br/>Reboot the device and try again.</div>";
const char HTTP_UPDATE_SUCCESS[] PROGMEM = "<div class='msg S'><strong>更新完成 Update successful</strong>部機而家重新開機緊。<br/>Device rebooting now.</div>";

#ifdef WM_JSTEST
const char HTTP_JS[] PROGMEM =
"<script>function postAjax(url, data, success) {"
"    var params = typeof data == 'string' ? data : Object.keys(data).map("
"            function(k){ return encodeURIComponent(k) + '=' + encodeURIComponent(data[k]) }"
"        ).join('&');"
"    var xhr = window.XMLHttpRequest ? new XMLHttpRequest() : new ActiveXObject(\"Microsoft.XMLHTTP\");"
"    xhr.open('POST', url);"
"    xhr.onreadystatechange = function() {"
"        if (xhr.readyState>3 && xhr.status==200) { success(xhr.responseText); }"
"    };"
"    xhr.setRequestHeader('X-Requested-With', 'XMLHttpRequest');"
"    xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
"    xhr.send(params);"
"    return xhr;}"
"postAjax('/status', 'p1=1&p2=Hello+World', function(data){ console.log(data); });"
"postAjax('/status', { p1: 1, p2: 'Hello World' }, function(data){ console.log(data); });"
"</script>";
#endif

// Info html — the page is not linked from YAT's menu (main.cpp prunes the menu
// to the Wi-Fi flow), so these stay in English: the only way to reach them is
// to type /info by hand, which is a developer, not a customer.
#ifdef ESP32
	const char HTTP_INFO_esphead[]    PROGMEM = "<h3>esp32</h3><hr><dl>";
	const char HTTP_INFO_chiprev[]    PROGMEM = "<dt>Chip rev</dt><dd>{1}</dd>";
  	const char HTTP_INFO_lastreset[]  PROGMEM = "<dt>Last reset reason</dt><dd>CPU0: {1}<br/>CPU1: {2}</dd>";
  	const char HTTP_INFO_aphost[]     PROGMEM = "<dt>Access point hostname</dt><dd>{1}</dd>";
    const char HTTP_INFO_psrsize[]    PROGMEM = "<dt>PSRAM Size</dt><dd>{1} bytes</dd>";
	const char HTTP_INFO_temp[]       PROGMEM = "<dt>Temperature</dt><dd>{1} C&deg; / {2} F&deg;</dd>";
    const char HTTP_INFO_hall[]       PROGMEM = "<dt>Hall</dt><dd>{1}</dd>";
#else
	const char HTTP_INFO_esphead[]    PROGMEM = "<h3>esp8266</h3><hr><dl>";
	const char HTTP_INFO_fchipid[]    PROGMEM = "<dt>Flash chip ID</dt><dd>{1}</dd>";
	const char HTTP_INFO_corever[]    PROGMEM = "<dt>Core version</dt><dd>{1}</dd>";
	const char HTTP_INFO_bootver[]    PROGMEM = "<dt>Boot version</dt><dd>{1}</dd>";
	const char HTTP_INFO_lastreset[]  PROGMEM = "<dt>Last reset reason</dt><dd>{1}</dd>";
	const char HTTP_INFO_flashsize[]  PROGMEM = "<dt>Real flash size</dt><dd>{1} bytes</dd>";
#endif

const char HTTP_INFO_memsmeter[]  PROGMEM = "<br/><progress value='{1}' max='{2}'></progress></dd>";
const char HTTP_INFO_memsketch[]  PROGMEM = "<dt>Memory - Sketch size</dt><dd>Used / Total bytes<br/>{1} / {2}";
const char HTTP_INFO_freeheap[]   PROGMEM = "<dt>Memory - Free heap</dt><dd>{1} bytes available</dd>";
const char HTTP_INFO_wifihead[]   PROGMEM = "<br/><h3>WiFi</h3><hr>";
const char HTTP_INFO_uptime[]     PROGMEM = "<dt>Uptime</dt><dd>{1} mins {2} secs</dd>";
const char HTTP_INFO_chipid[]     PROGMEM = "<dt>Chip ID</dt><dd>{1}</dd>";
const char HTTP_INFO_idesize[]    PROGMEM = "<dt>Flash size</dt><dd>{1} bytes</dd>";
const char HTTP_INFO_sdkver[]     PROGMEM = "<dt>SDK version</dt><dd>{1}</dd>";
const char HTTP_INFO_cpufreq[]    PROGMEM = "<dt>CPU frequency</dt><dd>{1}MHz</dd>";
const char HTTP_INFO_apip[]       PROGMEM = "<dt>Access point IP</dt><dd>{1}</dd>";
const char HTTP_INFO_apmac[]      PROGMEM = "<dt>Access point MAC</dt><dd>{1}</dd>";
const char HTTP_INFO_apssid[]     PROGMEM = "<dt>Access point SSID</dt><dd>{1}</dd>";
const char HTTP_INFO_apbssid[]    PROGMEM = "<dt>BSSID</dt><dd>{1}</dd>";
const char HTTP_INFO_stassid[]    PROGMEM = "<dt>Station SSID</dt><dd>{1}</dd>";
const char HTTP_INFO_staip[]      PROGMEM = "<dt>Station IP</dt><dd>{1}</dd>";
const char HTTP_INFO_stagw[]      PROGMEM = "<dt>Station gateway</dt><dd>{1}</dd>";
const char HTTP_INFO_stasub[]     PROGMEM = "<dt>Station subnet</dt><dd>{1}</dd>";
const char HTTP_INFO_dnss[]       PROGMEM = "<dt>DNS Server</dt><dd>{1}</dd>";
const char HTTP_INFO_host[]       PROGMEM = "<dt>Hostname</dt><dd>{1}</dd>";
const char HTTP_INFO_stamac[]     PROGMEM = "<dt>Station MAC</dt><dd>{1}</dd>";
const char HTTP_INFO_conx[]       PROGMEM = "<dt>Connected</dt><dd>{1}</dd>";
const char HTTP_INFO_autoconx[]   PROGMEM = "<dt>Autoconnect</dt><dd>{1}</dd>";

const char HTTP_INFO_aboutver[]     PROGMEM = "<dt>WiFiManager</dt><dd>{1}</dd>";
const char HTTP_INFO_aboutarduino[] PROGMEM = "<dt>Arduino</dt><dd>{1}</dd>";
const char HTTP_INFO_aboutsdk[]     PROGMEM = "<dt>ESP-SDK/IDF</dt><dd>{1}</dd>";
const char HTTP_INFO_aboutdate[]    PROGMEM = "<dt>Build date</dt><dd>{1}</dd>";

const char S_brand[]              PROGMEM = "YAT 日";
const char S_debugPrefix[]        PROGMEM = "*wm:";
const char S_y[]                  PROGMEM = "Yes";
const char S_n[]                  PROGMEM = "No";
const char S_enable[]             PROGMEM = "Enabled";
const char S_disable[]            PROGMEM = "Disabled";
const char S_GET[]                PROGMEM = "GET";
const char S_POST[]               PROGMEM = "POST";
const char S_NA[]                 PROGMEM = "Unknown";
const char S_passph[]             PROGMEM = "********";
// S_title* land in <title> — they show up as the phone's tab/window label and
// in the captive-portal sheet's header on iOS.
const char S_titlewifisaved[]     PROGMEM = "儲存咗 Saved";
const char S_titlewifisettings[]  PROGMEM = "儲存咗 Saved";
const char S_titlewifi[]          PROGMEM = "揀 Wi-Fi Choose Wi-Fi";
const char S_titleinfo[]          PROGMEM = "裝置資料 Device info";
const char S_titleparam[]         PROGMEM = "設定 Settings";
const char S_titleparamsaved[]    PROGMEM = "儲存咗 Saved";
const char S_titleexit[]          PROGMEM = "離開 Exit";
const char S_titlereset[]         PROGMEM = "重新開機 Restart";
const char S_titleerase[]         PROGMEM = "清除 Erase";
const char S_titleclose[]         PROGMEM = "閂咗 Closed";
const char S_options[]            PROGMEM = "設定 Setup";
const char S_nonetworks[]         PROGMEM = "<div class='msg D'><strong>搵唔到任何 Wi-Fi</strong>"
                                            "<p>行近部路由器啲，然後撳下面「再掃一次」。YAT 只搵到 2.4GHz 嘅 Wi-Fi。</p>"
                                            "<p class='en'>Move closer to your router, then tap Scan again below. "
                                            "YAT can only see 2.4 GHz networks.</p></div>";
const char S_staticip[]           PROGMEM = "Static IP";
const char S_staticgw[]           PROGMEM = "Static gateway";
const char S_staticdns[]          PROGMEM = "Static DNS";
const char S_subnet[]             PROGMEM = "Subnet";
const char S_exiting[]            PROGMEM = "離開緊 Exiting";
const char S_resetting[]          PROGMEM = "<div class='msg'><strong>就快重新開機</strong>"
                                            "<p>部機幾秒後會重新開機。</p>"
                                            "<p class='en'>The device restarts in a few seconds.</p></div>";
const char S_closing[]            PROGMEM = "<div class='msg'><strong>可以閂咗呢頁</strong>"
                                            "<p>設定頁仲開住，你隨時可以返嚟。</p>"
                                            "<p class='en'>You can close this page. Setup stays open if you need to come back.</p></div>";
const char S_error[]              PROGMEM = "<div class='msg D'><strong>出咗少少問題</strong>"
                                            "<p>請返上一頁再試一次。</p>"
                                            "<p class='en'>Something went wrong — go back and try again.</p></div>";
const char S_notfound[]           PROGMEM = "File not found\n\n";
const char S_uri[]                PROGMEM = "URI: ";
const char S_method[]             PROGMEM = "\nMethod: ";
const char S_args[]               PROGMEM = "\nArguments: ";
const char S_parampre[]           PROGMEM = "param_";

// debug strings
const char D_HR[]                 PROGMEM = "--------------------";

// softap ssid default prefix (YAT builds its own name in buildApCreds)
#ifdef ESP8266
    const char S_ssidpre[]        PROGMEM = "ESP";
#elif defined(ESP32)
    const char S_ssidpre[]        PROGMEM = "ESP32";
#else
    const char S_ssidpre[]        PROGMEM = "WM";
#endif

#endif  // _YAT_WM_STRINGS_H_
