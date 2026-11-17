// HeySticky network: saved Wi-Fi networks joined in the background, the HeySticky-Setup hotspot (04j)
// with the phone setup page, city lookup, weather (Open-Meteo) and NTP time.
// Known limit (PLAN.md §9): the setup page is plain HTTP, served only on the Sticky's own WPA2 hotspot.
#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include "store.h"
#include "wifi_list.h"
#include "lang.h"
#include "userfont.h"

constexpr char AP_SSID[] = "HeySticky-Setup";
constexpr uint32_t JOIN_TIMEOUT_MS = 15000, SCAN_TIMEOUT_MS = 15000, WIFI_RETRY_MS = 30000;
constexpr int SCAN_MAX = 20;

inline WebServer g_web(80);
inline DNSServer g_dns;
inline bool g_webOn = false;  // 04j toggle: hotspot + phone setup page
inline String g_apPass;       // new for every hotspot start
inline WifiList g_wifi;       // saved networks, flash keys wifi0…wifi4
inline ScanNet g_nets[SCAN_MAX];  // last scan, strongest entry per name. Not "g_scan": the ESP32 ROM owns that symbol
inline int g_netsN = 0;

enum NetState : uint8_t { NS_IDLE, NS_SCAN, NS_JOIN };
enum JoinResult : uint8_t { JR_NONE, JR_BUSY, JR_OK, JR_BADPASS, JR_FAIL };
inline NetState g_ns = NS_IDLE;
inline uint32_t g_nsAt = 0, g_retryAt = 0;
inline bool g_scanOnly = false, g_wasOn = false;
inline WifiNet g_try;            // network being joined
inline bool g_tryTest = false;   // asked from the phone page: report the result there
inline String g_jrSsid;
inline JoinResult g_jr = JR_NONE;
inline volatile uint8_t g_discReason = 0;  // set by the Wi-Fi event task
inline uint32_t g_wx_next = 0;   // millis() of the next weather fetch
inline long g_utc_off = LONG_MIN;  // seconds, from Open-Meteo; the last one is kept in flash (utc_off)

inline bool http_json(const String &url, JsonDocument &doc);
inline String url_enc(const String &s);

// ---------- saved networks ----------
inline void wifi_save() {
  for (int i = 0; i < WIFI_MAX; i++) {
    const String key = "wifi" + String(i);
    if (i < g_wifi.count) cfg_set(key.c_str(), wl_pack(g_wifi.n[i]));
    else cfg_del(key.c_str());
  }
}

inline void wifi_load() {
  g_wifi = WifiList();
  for (int i = 0; i < WIFI_MAX; i++) {
    const String v = cfg_get(("wifi" + String(i)).c_str());
    if (v.length()) g_wifi.n[g_wifi.count++] = wl_unpack(v);
  }
  const String old = cfg_get("wifi_ssid");  // before TASK-15: one network
  if (old.length()) {
    if (!g_wifi.count) { wl_add(g_wifi, old, cfg_get("wifi_pass")); wifi_save(); }
    cfg_del("wifi_ssid");
    cfg_del("wifi_pass");
  }
}

// ---------- background join ----------
inline void store_scan(int n) {
  if (n < 0) return;  // failed scan: keep the last list
  g_netsN = 0;
  for (int i = 0; i < n; i++) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;  // hidden
    int k = 0;
    while (k < g_netsN && g_nets[k].ssid != ssid) k++;
    if (k < g_netsN) { if (WiFi.RSSI(i) > g_nets[k].rssi) g_nets[k].rssi = WiFi.RSSI(i); }
    else if (g_netsN < SCAN_MAX) g_nets[g_netsN++] = ScanNet{ssid, int(WiFi.RSSI(i)), uint8_t(WiFi.encryptionType(i))};
  }
  WiFi.scanDelete();
  Serial.printf("[net] scan: %d networks\n", g_netsN);
}

inline void start_join(const WifiNet &w, bool test) {
  g_try = w;
  g_tryTest = test;
  g_discReason = 0;
  WiFi.disconnect();
  WiFi.begin(w.ssid.c_str(), w.pass.length() ? w.pass.c_str() : nullptr);
  g_ns = NS_JOIN;
  g_nsAt = millis();
  Serial.printf("[net] joining %s\n", w.ssid.c_str());
}

// Strongest saved network in the last scan; no new scan (a scan upsets the running hotspot).
inline bool join_best_cached() {
  const int k = wl_pick(g_wifi, g_nets, g_netsN);
  if (k >= 0) start_join(g_wifi.n[k], false);
  return k >= 0;
}

// One step of: scan -> join the strongest saved network -> online; retry every 30 s. Never blocks.
inline void net_sta_step() {
  const bool on = WiFi.status() == WL_CONNECTED;
  switch (g_ns) {
    case NS_IDLE:
      if (on) { g_wasOn = true; return; }
      if (g_wasOn) { g_wasOn = false; g_retryAt = millis(); Serial.println("[net] Wi-Fi lost"); }
      if (g_webOn || !g_wifi.count || millis() < g_retryAt) return;  // hotspot on: joins come from the page only
      WiFi.scanNetworks(true);
      g_scanOnly = false;
      g_ns = NS_SCAN;
      g_nsAt = millis();
      return;
    case NS_SCAN: {
      const int n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING && millis() - g_nsAt < SCAN_TIMEOUT_MS) return;
      store_scan(n);
      g_ns = NS_IDLE;
      if (g_scanOnly || on) return;
      if (!join_best_cached()) g_retryAt = millis() + WIFI_RETRY_MS;
      return;
    }
    case NS_JOIN: {
      if (on) {
        g_ns = NS_IDLE;
        g_wasOn = true;
        if (g_tryTest || g_wifi.n[0].ssid != g_try.ssid) { wl_add(g_wifi, g_try.ssid, g_try.pass); wifi_save(); }  // most recent first
        if (g_tryTest) g_jr = JR_OK;
        g_wx_next = 0;  // weather now
        Serial.printf("[net] wifi %s  ip=%s\n", g_try.ssid.c_str(), WiFi.localIP().toString().c_str());
        return;
      }
      const uint8_t r = g_discReason;
      g_discReason = 0;
      // The router turning the password down, as opposed to "not found" or a timeout.
      const bool bad = r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                       r == WIFI_REASON_HANDSHAKE_TIMEOUT || r == WIFI_REASON_MIC_FAILURE;
      if (!bad && millis() - g_nsAt < JOIN_TIMEOUT_MS) {
        if (r) WiFi.reconnect();  // one-off failures (busy router): try again until the timeout
        return;
      }
      Serial.printf("[net] join %s failed (%s, reason %d)\n", g_try.ssid.c_str(), bad ? "password" : "timeout", r);
      WiFi.disconnect();
      g_ns = NS_IDLE;
      g_retryAt = millis() + WIFI_RETRY_MS;
      if (g_tryTest) { g_jr = bad ? JR_BADPASS : JR_FAIL; join_best_cached(); }  // back to a saved network
      return;
    }
  }
}

// ---------- hotspot (04j) ----------
inline void net_hotspot(bool on) {
  if (on == g_webOn) return;
  if (on) {
    for (uint32_t t0 = millis(); g_ns == NS_SCAN && millis() - t0 < SCAN_TIMEOUT_MS; delay(50)) net_sta_step();
    // The page's network list. Scanning while softAP() starts crashed the Wi-Fi driver on core 3.3.12,
    // so the scan is taken here, just before (about 3 s).
    if (g_ns == NS_IDLE) store_scan(WiFi.scanNetworks());
    static const char A[] = "abcdefghjkmnpqrstuvwxyz23456789";  // no 0/o, 1/l/i: read off e-paper, typed by hand
    g_apPass = "";
    for (int i = 0; i < 8; i++) g_apPass += A[esp_random() % (sizeof A - 1)];
    WiFi.mode(WIFI_AP_STA);  // the home connection stays up next to the hotspot
    WiFi.softAP(AP_SSID, g_apPass.c_str());
    g_dns.start(53, "*", WiFi.softAPIP());  // captive portal: every name -> us
    g_web.begin();
  } else {
    g_web.stop();
    g_dns.stop();
    WiFi.softAPdisconnect(true);
    g_jr = JR_NONE;
    g_retryAt = millis();  // background joining resumes
  }
  g_webOn = on;
  Serial.printf("[net] hotspot %s\n", on ? "on" : "off");
}

// ---------- phone setup page (Figma 06–06f, 06l). One page; data through /api. ----------
// System sans-serif (no web font: the hotspot has no internet). Integrations (06g–06k) stay hidden until V2.
inline const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>HeySticky setup</title><style>
*{box-sizing:border-box}body{margin:0;background:#fff;color:#000;font:16px/1.25 -apple-system,system-ui,"Segoe UI",Roboto,sans-serif}
main{max-width:390px;margin:0 auto;min-height:100vh}
header{background:#000;color:#fff;height:90px;padding:0 20px;display:flex;align-items:center;justify-content:space-between;gap:12px}
h1{font-size:26px;margin:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}header p{font-size:14px;margin:2px 0 0}
button{font:inherit;color:#000;cursor:pointer}
.back{flex:none;width:40px;height:40px;border-radius:50%;border:2px solid #fff;background:none;color:#fff;font-size:24px;line-height:1;padding:0 2px 3px 0}
.c{padding:20px}[hidden]{display:none!important}
.g{border:2px solid #000;border-radius:8px;overflow:hidden;margin-bottom:22px}
.r{display:flex;align-items:center;gap:8px;width:100%;min-height:48px;padding:12px 14px;text-align:left;background:#fff;border:0;border-top:1px solid #000}
.r:first-child{border-top:0}.r .n{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.r b.n{font-weight:600}
.r .ck{flex:none;width:18px;height:18px}.r .i{background:none;border:0;padding:6px;margin:-6px -6px -6px 0;display:flex}
.r .ch{font-size:22px;line-height:1}svg{display:block;flex:none}
h2{font-size:13px;font-weight:600;margin:0 0 8px}.note{font-size:13px;margin:-14px 0 22px}.note button{background:none;border:0;padding:0;text-decoration:underline;font-size:13px}
label{display:block;font-size:14px;font-weight:600;margin:10px 0 6px}label:first-child{margin-top:0}
input{width:100%;height:47px;font:inherit;padding:0 14px;border:2px solid #000;border-radius:8px;background:#fff;color:#000}
.city{position:relative}.sug{position:absolute;left:0;right:0;top:100%;margin-top:4px;background:#fff;border:2px solid #000;border-radius:8px;z-index:1;overflow:hidden}
.sug button{display:block;width:100%;text-align:left;padding:10px 14px;background:#fff;border:0;border-top:1px solid #000}
.sug button:first-child{border-top:0}.sug b,.sug small{display:block}.sug small{font-size:13px;margin-top:2px}
.btn{display:block;width:100%;height:54px;font-size:18px;font-weight:700;border:2px solid #000;border-radius:8px;background:#fff}
.btn.k{color:#fff;background:#000;margin-top:22px}
.dim{position:fixed;inset:0;background:rgba(0,0,0,.45);z-index:5}
.sheet{position:fixed;left:0;right:0;top:56px;bottom:0;max-width:390px;margin:0 auto;background:#fff;border-radius:28px 28px 0 0;padding:20px 24px;z-index:6;overflow:auto}
.bar{display:flex;justify-content:space-between}.rb{width:44px;height:44px;border-radius:50%;border:2px solid #000;background:#fff;font-size:20px;padding:0}.rb.k{background:#000;color:#fff}
.big{display:flex;justify-content:center;margin:36px 0 40px}.sheet h3{font-size:22px;margin:0 0 4px}.sheet p{font-size:18px;margin:0 0 40px}
.sheet input{height:54px;border-radius:27px;padding:0 22px;font-size:17px;margin-bottom:12px}
.al{position:fixed;left:50%;top:50%;transform:translate(-50%,-50%);width:300px;max-width:calc(100% - 40px);background:#fff;border:2px solid #000;border-radius:14px;z-index:8;text-align:center;overflow:hidden}
.al h3{font-size:17px;margin:20px 16px 4px}.al p{font-size:14px;margin:0 16px 18px}.al div{display:flex;border-top:2px solid #000}
.al button{flex:1;height:48px;background:#fff;border:0;font-size:17px}.al button+button{border-left:2px solid #000;font-weight:700}.al button:only-child{font-weight:700}
</style></head><body><main>
<header><div><h1 id="ttl">HeySticky setup</h1><p id="sub">Connected to your Sticky · 192.168.4.1</p></div><button class="back" id="back" hidden aria-label="Back">‹</button></header>
<div class="c" id="v-setup"><div class="g">
<button class="r" id="rp"><b class="n">Profile</b><span id="pv"></span><span class="ch">›</span></button>
<button class="r" id="rw"><b class="n">Wi-Fi</b><span id="wv"></span><span class="ch">›</span></button></div></div>
<form class="c" id="v-profile" hidden>
<label for="name">Your name</label><input id="name" maxlength="32">
<label for="city">City (for weather)</label><div class="city"><input id="city" maxlength="64" autocomplete="off"><div class="sug" id="sug" hidden></div></div>
<button class="btn k" id="save">✓&nbsp; Save to Sticky</button></form>
<div class="c" id="v-wifi" hidden></div>
<div class="c" id="v-det" hidden></div>
</main><div id="ov"></div>
<script>
const $=i=>document.getElementById(i),E=(t,p,...k)=>{const e=Object.assign(document.createElement(t),p||{});e.append(...k);return e};
const H=(s)=>{const d=E('span');d.innerHTML=s;return d.firstChild};
const svg=(w,b)=>'<svg viewBox="0 0 24 24" width="'+w+'" height="'+w+'" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round">'+b+'</svg>';
const wifi=(l,w)=>H(svg(w||20,'<path d="M2 9a15 15 0 0 1 20 0" opacity="'+(l>2?1:.25)+'"/><path d="M5.5 12.6a10 10 0 0 1 13 0" opacity="'+(l>1?1:.25)+'"/><path d="M9 16.2a5 5 0 0 1 6 0"/><circle cx="12" cy="19.6" r="1.3" fill="currentColor" stroke="none"/>'));
const lock=()=>H(svg(16,'<rect x="5" y="11" width="14" height="10" rx="2" fill="currentColor"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>'));
const info=()=>H(svg(20,'<circle cx="12" cy="12" r="9.5"/><path d="M12 11v6"/><circle cx="12" cy="7.5" r=".6" fill="currentColor"/>'));
const check=()=>H(svg(18,'<path d="M4.5 12.5l5 5L19.5 7"/>'));
const ENC=['None','WEP','WPA','WPA2','WPA/WPA2','WPA2 Enterprise','WPA3','WPA2/WPA3'];
const lvl=r=>r>=-60?3:r>=-75?2:1;
let S={saved:[],scan:[]},view='setup',det='',pending=false,lat='',lon='';
const api=(p,b)=>fetch(p,b?{method:'POST',body:new URLSearchParams(b)}:{});
const seen=s=>S.scan.find(n=>n.ssid===s);

async function load(){try{S=await(await api('/api/status')).json()}catch(e){return}
 if(pending&&S.join&&S.join.state!=='busy'){pending=false;
  if(S.join.state==='badpass')alertBox('Incorrect password for “'+S.join.ssid+'”');
  else if(S.join.state==='fail')alertBox('Couldn\'t join “'+S.join.ssid+'”')}
 render()}

function go(v,d){view=v;det=d||'';
 if(v==='profile'){$('name').value=S.name||'';$('city').value=S.city||'';lat=lon='';$('sug').hidden=true}
 render();scrollTo(0,0)}
$('back').onclick=()=>go(view==='det'?'wifi':'setup');
$('rp').onclick=()=>go('profile');$('rw').onclick=()=>go('wifi');

function row(n,o){const joining=S.join&&S.join.state==='busy'&&S.join.ssid===n.ssid;
 const r=E('div',{className:'r'}),ck=E('span',{className:'ck'});if(o.cur)ck.append(check());
 const nm=E(o.cur?'b':'span',{className:'n',textContent:n.ssid});r.append(ck,nm);
 if(joining)r.append(E('span',{textContent:'Joining…'}));
 else{if(n.enc)r.append(lock());if(n.rssi!=null)r.append(wifi(lvl(n.rssi)));
  if(o.info){const b=E('button',{className:'i',ariaLabel:'Details'},info());b.onclick=e=>{e.stopPropagation();go('det',n.ssid)};r.append(b)}}
 if(o.tap)r.onclick=o.tap;return r}

function render(){
 const cur=S.cur?S.cur.ssid:'';
 for(const v of['setup','profile','wifi','det'])$('v-'+v).hidden=v!==view;
 $('back').hidden=view==='setup';$('sub').hidden=view!=='setup';
 $('ttl').textContent=view==='setup'?'HeySticky setup':view==='profile'?'Profile':view==='wifi'?'Wi-Fi':det;
 $('pv').textContent=S.name||'';$('wv').textContent=cur||'Not connected';
 if(view==='wifi'){const w=$('v-wifi');w.replaceChildren();
  const net=s=>Object.assign({ssid:s},seen(s)||{});
  if(cur)w.append(E('div',{className:'g'},row(Object.assign(net(cur),{rssi:S.cur.rssi}),{cur:1,info:1})));
  const my=S.saved.filter(s=>s!==cur);
  if(my.length)w.append(E('h2',{textContent:'MY NETWORKS'}),E('div',{className:'g'},...my.map(s=>row(net(s),{info:1,tap:()=>join(s,'')}))));
  const other=S.scan.filter(n=>n.ssid!==cur&&!S.saved.includes(n.ssid));
  const o=E('div',{className:'r'},E('span',{className:'ck'}),E('span',{className:'n',textContent:'Other…'}));o.onclick=()=>sheet('');
  w.append(E('h2',{textContent:'OTHER NETWORKS'}),E('div',{className:'g'},...other.map(n=>row(n,{tap:()=>n.enc?sheet(n.ssid):join(n.ssid,'')})),o));
  const rf=E('button',{textContent:S.scanning?'Looking…':'Refresh'});rf.onclick=async()=>{try{await api('/api/scan',{})}catch(e){}load()};
  w.append(E('p',{className:'note'},'Found when Web setup was turned on. ',rf))}
 if(view==='det'){const d=$('v-det');d.replaceChildren();const n=seen(det),on=det===cur;
  const kv=(k,v)=>E('div',{className:'r'},E('b',{className:'n',textContent:k}),E('span',{textContent:v}));
  const rs=on?S.cur.rssi:n?n.rssi:null;
  const g=E('div',{className:'g'},kv('Status',on?'Connected':'Saved'),kv('Signal',rs==null?'Not in range':['','Weak','Good','Excellent'][lvl(rs)]));
  if(n)g.append(kv('Security',ENC[n.enc]||'Secured'));if(on)g.append(kv('IP address',S.cur.ip));
  const f=E('button',{className:'btn',textContent:'Forget This Network'});
  f.onclick=()=>alertBox('Forget “'+det+'”?','Your Sticky will stop joining it.','Forget',async()=>{try{await api('/api/forget',{ssid:det})}catch(e){}go('wifi');load()});
  d.append(g,f)}}

async function join(ssid,pass){pending=true;S.join={ssid,state:'busy'};render();
 try{await api('/api/join',{ssid,pass})}catch(e){}  // the hotspot can drop for a moment while the Sticky changes channel
 }

function close(){$('ov').replaceChildren()}
function sheet(ssid){const pw=E('input',{type:'password',placeholder:'Password',maxLength:63,autocomplete:'off'});
 const nm=E('input',{placeholder:'Network name',maxLength:32,autocomplete:'off',autocapitalize:'off'});
 const x=E('button',{className:'rb',textContent:'✕',ariaLabel:'Cancel'}),ok=E('button',{className:'rb k',textContent:'✓',ariaLabel:'Join'});
 const sub=()=>{const s=ssid||nm.value.trim();if(!s)return;close();join(s,pw.value)};
 x.onclick=close;ok.onclick=sub;pw.onkeydown=nm.onkeydown=e=>{if(e.key==='Enter')sub()};
 const sh=E('div',{className:'sheet'},E('div',{className:'bar'},x,ok),E('div',{className:'big'},wifi(3,88)),
  E('h3',{textContent:ssid?'Join “'+ssid+'”':'Join another network'}),
  E('p',{textContent:ssid?'Enter the password to join this Wi-Fi network.':'Enter the name and password of the Wi-Fi network.'}));
 if(!ssid)sh.append(nm);sh.append(pw);
 $('ov').replaceChildren(E('div',{className:'dim',onclick:close}),sh);(ssid?pw:nm).focus()}
function alertBox(title,text,yes,fn){const b=E('div'),a=E('div',{className:'al'},E('h3',{textContent:title}));
 if(text)a.append(E('p',{textContent:text}));else a.firstChild.style.marginBottom='18px';
 if(yes){b.append(E('button',{textContent:'Cancel',onclick:close}),E('button',{textContent:yes,onclick:()=>{close();fn()}}))}
 else b.append(E('button',{textContent:'OK',onclick:close}));
 a.append(b);$('ov').replaceChildren(E('div',{className:'dim'}),a)}

// City suggestions: through the Sticky when it is online, else straight from the phone (if it has another way out).
// Picking one sends its lat/lon, so the Sticky never guesses between towns with the same name.
const c=$('city'),l=$('sug');let t;
async function places(q){try{const r=await api('/api/city?q='+encodeURIComponent(q));if(r.ok)return await r.json()}catch(e){}
 const d=await(await fetch('https://geocoding-api.open-meteo.com/v1/search?count=5&name='+encodeURIComponent(q))).json();
 return(d.results||[]).map(p=>({name:p.name,region:[p.admin1,p.country].filter(Boolean).join(', '),lat:p.latitude,lon:p.longitude}))}
c.oninput=()=>{lat=lon='';clearTimeout(t);const q=c.value.trim();
 if(q.length<2){l.hidden=true;return}
 t=setTimeout(async()=>{try{const ps=await places(q);if(c.value.trim()!==q)return;l.replaceChildren();
  for(const p of ps){const b=E('button',{type:'button'},E('b',{textContent:p.name}),E('small',{textContent:p.region}));
   b.onclick=()=>{c.value=p.name;lat=p.lat;lon=p.lon;l.hidden=true};l.append(b)}
  l.hidden=!l.children.length}catch(e){l.hidden=true}},300)};
document.addEventListener('click',e=>{if(!l.contains(e.target)&&e.target!==c)l.hidden=true});
$('v-profile').onsubmit=async e=>{e.preventDefault();const b=$('save');
 try{const r=await api('/api/save',{name:$('name').value,city:c.value,lat,lon});if(!r.ok)throw 0;
  b.textContent='✓  Saved';await load();setTimeout(()=>{b.textContent='✓  Save to Sticky';go('setup')},900)}
 catch(e){alertBox('Couldn\'t save')}};
load();setInterval(load,1500);
</script></body></html>)HTML";

// The page and its API answer only on the hotspot's own address, never on the home network.
inline bool ap_only() {
  if (g_web.client().localIP() == WiFi.softAPIP()) return true;
  g_web.send(404, "text/plain", "Not found");
  return false;
}

inline void send_json(const JsonDocument &d, int code = 200) {
  String s;
  serializeJson(d, s);
  g_web.send(code, "application/json", s);
}

// Trust boundary: text from the phone. Length limit, no control characters (a line break would split a flash value).
inline bool text_ok(const String &s, size_t maxLen) {
  if (s.length() > maxLen) return false;
  for (const char c : s)
    if (uint8_t(c) < 0x20 || c == 0x7f) return false;
  return true;
}

inline void api_status() {
  if (!ap_only()) return;
  JsonDocument d;
  d["name"] = cfg_get("name");
  d["city"] = cfg_get("city");
  if (WiFi.status() == WL_CONNECTED) {
    JsonObject c = d["cur"].to<JsonObject>();
    c["ssid"] = WiFi.SSID();
    c["rssi"] = WiFi.RSSI();
    c["ip"] = WiFi.localIP().toString();
  }
  JsonArray sv = d["saved"].to<JsonArray>();  // names only: passwords never go back to the browser
  for (int i = 0; i < g_wifi.count; i++) sv.add(g_wifi.n[i].ssid);
  JsonArray sc = d["scan"].to<JsonArray>();
  for (int i = 0; i < g_netsN; i++) {
    JsonObject n = sc.add<JsonObject>();
    n["ssid"] = g_nets[i].ssid;
    n["rssi"] = g_nets[i].rssi;
    n["enc"] = g_nets[i].enc;
  }
  d["scanning"] = g_ns == NS_SCAN;
  d["lang"] = g_vi ? "vi" : "en";
  if (g_jr != JR_NONE) {
    static const char *const ST[] = {"", "busy", "ok", "badpass", "fail"};
    d["join"]["ssid"] = g_jrSsid;
    d["join"]["state"] = ST[g_jr];
  }
  send_json(d);
}

inline void api_reply(int code, const char *msg) { g_web.send(code, "text/plain", msg); }

// Test-join: the hotspot stays up, the result is read from /api/status. A saved network needs no password.
inline void api_join() {
  if (!ap_only()) return;
  String ssid = g_web.arg("ssid"), pass = g_web.arg("pass");
  if (ssid.isEmpty() || !text_ok(ssid, 32) || !text_ok(pass, 63)) return api_reply(400, "Invalid input");
  if (g_ns == NS_SCAN) return api_reply(409, "Busy");
  const int k = wl_find(g_wifi, ssid);
  if (pass.isEmpty() && k >= 0) pass = g_wifi.n[k].pass;
  g_jrSsid = ssid;
  g_jr = JR_BUSY;
  api_reply(202, "Joining");  // answer first: joining can move the hotspot to another channel
  start_join(WifiNet{ssid, pass}, true);
}

inline void api_forget() {
  if (!ap_only()) return;
  const String ssid = g_web.arg("ssid");
  if (!wl_forget(g_wifi, ssid)) return api_reply(404, "Not saved");
  wifi_save();
  if (g_jrSsid == ssid) g_jr = JR_NONE;
  if (WiFi.SSID() == ssid || (g_ns == NS_JOIN && g_try.ssid == ssid)) {
    WiFi.disconnect();
    g_ns = NS_IDLE;
    g_wasOn = false;
    join_best_cached();
  }
  api_reply(200, "OK");
}

// Name + city. lat/lon only as numbers in range, both or neither (a picked suggestion). No restart.
inline void api_save() {
  if (!ap_only()) return;
  String name = g_web.arg("name"), city = g_web.arg("city"), lat = g_web.arg("lat"), lon = g_web.arg("lon");
  name.trim(); city.trim();
  char *e1 = nullptr, *e2 = nullptr;
  const double la = strtod(lat.c_str(), &e1), lo = strtod(lon.c_str(), &e2);
  const bool picked = lat.length() && lon.length();
  if (!text_ok(name, 32) || !text_ok(city, 64) || (lat.length() > 0) != (lon.length() > 0) ||
      (picked && (*e1 || *e2 || !(la >= -90 && la <= 90) || !(lo >= -180 && lo <= 180) || city.isEmpty())))  // !(in range) also rejects nan
    return api_reply(400, "Invalid input");
  cfg_set("name", name);
  cfg_set("city", city);
  if (picked) {  // exact place: no lookup by name
    cfg_set("lat", String(la, 4));
    cfg_set("lon", String(lo, 4));
    cfg_set("geo_city", city);
  }
  g_wx_next = 0;  // weather for the new city now
  api_reply(200, "OK");
}

// Refresh of the network list while the hotspot runs.
inline void api_scan() {
  if (!ap_only()) return;
  if (g_ns != NS_IDLE) return api_reply(409, "Busy");
  WiFi.scanNetworks(true);
  g_scanOnly = true;
  g_ns = NS_SCAN;
  g_nsAt = millis();
  api_reply(202, "Scanning");
}

// ---------- user fonts (06m, 06n): the phone converts, the Sticky only stores and checks ----------
inline void api_fonts() {
  if (!ap_only()) return;
  JsonDocument d;
  d["cur"] = cfg_get("font");
  d["sd"] = g_sdOk;
  JsonArray l = d["list"].to<JsonArray>();
  for (const String &n : font_list()) l.add(n);
  send_json(d);
}

// One slot file per request (multipart): /api/font?name=NotoSans&slot=R25. Written as .tmp, checked, then renamed.
inline File g_upFile;
inline bool g_upOk = false;
inline size_t g_upN = 0;
inline void api_font_part() {
  HTTPUpload &u = g_web.upload();
  const String name = g_web.arg("name"), slot = g_web.arg("slot");
  const String tmp = font_path(name, slot.c_str(), ".tmp");
  if (u.status == UPLOAD_FILE_START) {
    g_upN = 0;
    g_upOk = g_web.client().localIP() == WiFi.softAPIP() && g_sdOk && font_name_ok(name) && font_slot_ok(slot);
    if (g_upOk) { SD.mkdir("/fonts"); SD.mkdir("/fonts/" + name); g_upFile = SD.open(tmp, FILE_WRITE); g_upOk = bool(g_upFile); }
  } else if (u.status == UPLOAD_FILE_WRITE) {
    g_upN += u.currentSize;
    if (g_upOk) g_upOk = g_upN <= FONT_FILE_MAX && g_upFile.write(u.buf, u.currentSize) == u.currentSize;
  } else {  // END or ABORTED
    if (g_upFile) g_upFile.close();
    size_t n;
    uint8_t *b = g_upOk && u.status == UPLOAD_FILE_END ? font_read(tmp, n) : nullptr;
    g_upOk = b != nullptr;
    free(b);
    const String path = font_path(name, slot.c_str());
    if (g_upOk) { SD.remove(path); g_upOk = SD.rename(tmp, path); }
    if (!g_upOk && font_name_ok(name) && font_slot_ok(slot)) SD.remove(tmp);
  }
}
inline void api_font_done() {
  if (!ap_only()) return;
  if (g_upOk && cfg_get("font") == g_web.arg("name") && g_web.arg("slot") == FONT_SLOTS[FONT_SLOTS_N - 1].file) {
    font_apply(g_web.arg("name"));  // the font in use was replaced: take the new files after the last slot
    g_fontDirty = true;
  }
  api_reply(g_upOk ? 200 : 400, g_upOk ? "OK" : "Bad font file");
  g_upOk = false;
}

inline void api_font_del() {
  if (!ap_only()) return;
  api_reply(font_delete(g_web.arg("name")) ? 200 : 400, "");
}

// City suggestions through the Sticky's own Wi-Fi (the phone on the hotspot has no internet).
inline void api_city() {
  if (!ap_only()) return;
  String q = g_web.arg("q");
  q.trim();
  if (q.length() < 2 || !text_ok(q, 64)) return api_reply(400, "Invalid input");
  if (WiFi.status() != WL_CONNECTED) return api_reply(503, "Offline");
  JsonDocument d, out;
  if (!http_json("https://geocoding-api.open-meteo.com/v1/search?count=5&name=" + url_enc(q), d)) return api_reply(502, "Lookup failed");
  JsonArray a = out.to<JsonArray>();
  for (JsonObject r : d["results"].as<JsonArray>()) {
    JsonObject p = a.add<JsonObject>();
    p["name"] = r["name"];
    String region = r["admin1"] | "";
    const char *country = r["country"] | "";
    if (region.length() && *country) region += ", ";
    p["region"] = region + country;
    p["lat"] = r["latitude"];
    p["lon"] = r["longitude"];
  }
  send_json(out);
}

// Returns true when a network is saved; false = first setup, the hotspot is already on.
inline bool net_begin() {
  WiFi.persistent(false);  // our own store.h holds the networks
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // net_sta_step() picks the strongest saved network itself
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    const uint8_t r = info.wifi_sta_disconnected.reason;
    if (r != WIFI_REASON_ASSOC_LEAVE) g_discReason = r;  // not our own disconnect()
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  wifi_load();
  const String off = cfg_get("utc_off");  // last known offset: right clock before the first weather reply
  if (off.length()) g_utc_off = off.toInt();
  configTime(off.toInt(), 0, "pool.ntp.org", "time.google.com");
  g_web.on("/", HTTP_GET, [] { if (ap_only()) g_web.send_P(200, "text/html; charset=utf-8", PAGE); });
  g_web.on("/api/status", HTTP_GET, api_status);
  g_web.on("/api/city", HTTP_GET, api_city);
  g_web.on("/api/join", HTTP_POST, api_join);
  g_web.on("/api/forget", HTTP_POST, api_forget);
  g_web.on("/api/save", HTTP_POST, api_save);
  g_web.on("/api/scan", HTTP_POST, api_scan);
  g_web.on("/api/fonts", HTTP_GET, api_fonts);
  g_web.on("/api/font", HTTP_POST, api_font_done, api_font_part);
  g_web.on("/api/font_del", HTTP_POST, api_font_del);
  g_web.onNotFound([] {  // captive portal: any other address -> the page
    if (!ap_only()) return;
    g_web.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
    g_web.send(302, "text/plain", "");
  });
  Serial.printf("[net] %d saved network(s)\n", g_wifi.count);
  if (!g_wifi.count) net_hotspot(true);
  return g_wifi.count > 0;
}

// ---------- HTTPS JSON (certificates checked against the core's built-in root CA bundle) ----------
inline bool http_json(const String &url, JsonDocument &doc) {
  NetworkClientSecure tls;
  tls.useBuiltinCACertBundle();
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(tls, url)) return false;
  const int code = http.GET();
  const bool ok = code == 200 && !deserializeJson(doc, http.getString());
  if (!ok) Serial.printf("[http] %d %s\n", code, url.c_str());
  http.end();
  return ok;
}

inline String url_enc(const String &s) {
  String o;
  for (uint8_t c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += char(c);
    else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); o += b; }
  }
  return o;
}

// City -> lat/lon/timezone, only when the saved city changed and wasn't picked from the
// phone page's suggestions. Top match for the name.
inline bool net_geocode() {
  const String city = cfg_get("city");
  if (city.isEmpty()) return false;
  if (cfg_get("geo_city") == city && cfg_get("lat").length()) return true;
  JsonDocument d;
  if (!http_json("https://geocoding-api.open-meteo.com/v1/search?count=1&name=" + url_enc(city), d)) return false;
  JsonObject r = d["results"][0];
  if (r.isNull()) { Serial.printf("[geo] no match for %s\n", city.c_str()); return false; }
  cfg_set("lat", String(r["latitude"].as<float>(), 4));
  cfg_set("lon", String(r["longitude"].as<float>(), 4));
  cfg_set("tz", r["timezone"].as<String>());
  cfg_set("geo_city", city);
  Serial.printf("[geo] %s -> %s, %s  %s\n", city.c_str(), cfg_get("lat").c_str(), cfg_get("lon").c_str(), cfg_get("tz").c_str());
  return true;
}

// ---------- weather ----------
enum WxIcon : uint8_t { WX_SUN, WX_PARTLY, WX_CLOUD, WX_FOG, WX_RAIN, WX_SNOW, WX_STORM };
struct WxInfo { const char *label; WxIcon icon; };

// WMO weather code -> short label (EN/VI) + icon type.
inline WxInfo wx_info(int code) {
  if (code == 0) return {TR("Clear", "Trời quang"), WX_SUN};
  if (code == 1) return {TR("Mainly clear", "Ít mây"), WX_SUN};
  if (code == 2) return {TR("Partly cloudy", "Có mây"), WX_PARTLY};
  if (code == 3) return {TR("Overcast", "Nhiều mây"), WX_CLOUD};
  if (code == 45 || code == 48) return {TR("Fog", "Sương mù"), WX_FOG};
  if (code >= 51 && code <= 57) return {TR("Drizzle", "Mưa phùn"), WX_RAIN};
  if (code >= 61 && code <= 67) return {TR("Rain", "Mưa"), WX_RAIN};
  if (code >= 71 && code <= 77) return {TR("Snow", "Tuyết"), WX_SNOW};
  if (code >= 80 && code <= 82) return {TR("Showers", "Mưa rào"), WX_RAIN};
  if (code == 85 || code == 86) return {TR("Snow showers", "Mưa tuyết"), WX_SNOW};
  if (code >= 95) return {TR("Thunderstorm", "Dông"), WX_STORM};
  return {TR("Unknown", "Không rõ"), WX_CLOUD};
}

struct Weather { bool ok = false; float temp = 0, hi = 0, lo = 0; int code = 0; uint32_t at = 0; };
inline Weather g_wx;               // ok=false -> show "Offline"
constexpr uint32_t WX_EVERY_MS = 30UL * 60 * 1000, WX_RETRY_MS = 60UL * 1000;

inline bool net_weather() {
  if (WiFi.status() != WL_CONNECTED || !net_geocode()) return false;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + cfg_get("lat") + "&longitude=" + cfg_get("lon") +
               "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1";
  if (cfg_get("units", "C") == "F") url += "&temperature_unit=fahrenheit";
  JsonDocument d;
  if (!http_json(url, d)) return false;
  g_wx = {true, d["current"]["temperature_2m"].as<float>(), d["daily"]["temperature_2m_max"][0].as<float>(),
          d["daily"]["temperature_2m_min"][0].as<float>(), d["current"]["weather_code"].as<int>(), millis()};
  const long off = d["utc_offset_seconds"].as<long>();
  if (off != g_utc_off) {  // follows DST within 30 min; kept for the next boot
    g_utc_off = off;
    cfg_set("utc_off", String(off));
    configTime(off, 0, "pool.ntp.org", "time.google.com");
  }
  Serial.printf("[wx] %.1f (%.0f/%.0f) code=%d %s  utc%+ld\n", g_wx.temp, g_wx.hi, g_wx.lo, g_wx.code, wx_info(g_wx.code).label, off);
  return true;
}

inline void net_loop() {
  net_sta_step();
  if (g_webOn) { g_dns.processNextRequest(); g_web.handleClient(); }
  if (WiFi.status() == WL_CONNECTED && millis() >= g_wx_next) {
    const bool ok = net_weather();
    g_wx_next = millis() + (ok ? WX_EVERY_MS : WX_RETRY_MS);
  }
  if (g_wx.ok && millis() - g_wx.at > WX_EVERY_MS + 2 * WX_RETRY_MS) g_wx.ok = false;  // stale -> Offline
}
