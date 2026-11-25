// HeySticky network: saved Wi-Fi networks joined in the background, the HeySticky-Setup hotspot (04j)
// with the phone setup page, city lookup, weather (Open-Meteo) and NTP time.
// Known limit (PLAN.md §9): the setup page is plain HTTP, on the Sticky's own WPA2 hotspot or, after a warning on
// 04j, on the home network (anyone on that Wi-Fi can open it).
#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_sntp.h>
#include "store.h"
#include "wifi_list.h"
#include "src/webfont.h"
#include "lang.h"
#include "userfont.h"

constexpr char AP_SSID[] = "HeySticky-Setup";
constexpr uint32_t JOIN_TIMEOUT_MS = 15000, SCAN_TIMEOUT_MS = 15000, WIFI_RETRY_MS = 30000;
constexpr int SCAN_MAX = 20;

inline WebServer g_web(80);
inline DNSServer g_dns;
inline String g_apPass;       // new for every hotspot start
inline bool g_webHome = false;  // 04j Home Wi-Fi: page on the home network address, no hotspot
inline WifiList g_wifi;       // saved networks, flash keys wifi0…wifi4
inline ScanNet g_nets[SCAN_MAX];  // last scan, strongest entry per name. Not "g_scan": the ESP32 ROM owns that symbol
inline int g_netsN = 0;

enum NetState : uint8_t { NS_IDLE, NS_SCAN, NS_JOIN };
enum JoinResult : uint8_t { JR_NONE, JR_BUSY, JR_OK, JR_BADPASS, JR_FAIL };
inline NetState g_ns = NS_IDLE;
inline uint32_t g_nsAt = 0, g_retryAt = 0;
inline bool g_scanOnly = false, g_wasOn = false;
inline bool g_wifiOn = true;  // 06b Wi-Fi switch (flash "wifi_on" = "0" when off): off = never join a network
inline WifiNet g_try;            // network being joined
inline bool g_tryTest = false;   // asked from the phone page: report the result there
inline String g_jrSsid;
inline JoinResult g_jr = JR_NONE;
inline volatile uint8_t g_discReason = 0;  // set by the Wi-Fi event task
inline uint32_t g_wx_next = 0;   // millis() of the next weather fetch
inline volatile bool g_ntpOk = false;  // an NTP reply came since boot (then HTTP Date headers are not needed)
inline volatile time_t g_netTime = 0;  // UTC just taken from the network, waiting for net_loop() to store it in the RTC
inline long g_utc_off = LONG_MIN;  // seconds, from Open-Meteo; the last one is kept in flash (utc_off)
inline time_t g_bootClock = 0;  // RTC time at boot minus uptime: net_loop() logs how far off it was at the first network time

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
      if (!g_wifiOn || (g_webOn && !g_webHome) || !g_wifi.count || millis() < g_retryAt) return;  // hotspot on: joins come from the page only
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
      if (g_scanOnly || on || !g_wifiOn) return;
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
inline uint32_t g_webSeen = 0;  // millis() of the last request from the phone page (it asks for /api/status every 1.5 s)

// 04j: the phone page on the hotspot (home = false) or on the home network address (home = true, no hotspot).
inline void net_web(bool on, bool home = false) {
  if (on == g_webOn && (!on || home == g_webHome)) return;
  g_webSeen = millis();
  if (g_webOn) {  // stop what runs now (also before switching mode)
    g_web.stop();
    if (!g_webHome) { g_dns.stop(); WiFi.softAPdisconnect(true); }
    g_jr = JR_NONE;
    g_retryAt = millis();  // background joining resumes
    g_webOn = false;
  }
  if (on) {
    if (!home) {
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
    }
    g_web.begin();
    g_webHome = home;
    g_webOn = true;
  }
  Serial.printf("[net] web setup %s%s\n", on ? "on " : "off", on ? (home ? "home Wi-Fi " + WiFi.localIP().toString() : String("hotspot")).c_str() : "");
}

// ---------- phone setup page (Figma 06–06f, 06l). One page; data through /api. ----------
// JetBrains Mono, served by the Sticky itself (/font/r.woff, /font/b.woff from src/webfont.h): the hotspot has no internet. Integrations (06g–06k) stay hidden until Phase 2.
inline const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>HeySticky setup</title><style>
@font-face{font-family:JBM;src:url(/font/r.woff) format("woff");font-weight:400}@font-face{font-family:JBM;src:url(/font/b.woff) format("woff");font-weight:600 800}
*{box-sizing:border-box}body{margin:0;background:#fff;color:#000;font:16px/1.25 JBM,ui-monospace,monospace}
main{min-height:100vh}
header{background:#000;color:#fff;height:90px;padding:0 20px;display:flex;align-items:center;justify-content:space-between;gap:12px}
header>div{flex:1;min-width:0}h1{font-size:26px;margin:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}header p{font-size:14px;margin:2px 0 0}
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
.sheet{position:fixed;left:0;right:0;top:56px;bottom:0;background:#fff;border-radius:28px 28px 0 0;padding:20px 24px;z-index:6;overflow:auto}
.bar{display:flex;justify-content:space-between}.rb{width:44px;height:44px;border-radius:50%;border:2px solid #000;background:#fff;font-size:20px;padding:0}.rb.k{background:#000;color:#fff}
.warn{display:flex;align-items:flex-start;gap:10px;border:2px solid #000;border-radius:8px;padding:12px 14px;margin-bottom:22px}.warn b{display:block;font-size:14px}.warn p{font-size:13px;margin:2px 0 0}
.big{display:flex;justify-content:center;margin:36px 0 40px}.sheet h3{font-size:22px;margin:0 0 4px}.sheet p{font-size:18px;margin:0 0 40px}
.sheet input{height:54px;border-radius:27px;padding:0 22px;font-size:17px;margin-bottom:12px}
.al{position:fixed;left:50%;top:50%;transform:translate(-50%,-50%);width:300px;max-width:calc(100% - 40px);background:#fff;border:2px solid #000;border-radius:14px;z-index:8;text-align:center;overflow:hidden}
.al h3{font-size:17px;margin:20px 16px 4px}.al p{font-size:14px;margin:0 16px 18px}.al div{display:flex;border-top:2px solid #000}
.warn{border:2px solid #000;border-radius:8px;padding:12px 14px;font-size:14px;font-weight:600;margin:0 0 22px}
.pv{border:1px solid #000;border-radius:8px;padding:16px;margin:0 0 22px}.pv small{display:block;font-size:12px;margin:12px 0 2px}.pv small:first-child{margin-top:0}
.pv canvas{display:block;max-width:100%;image-rendering:pixelated}.r .ib{width:32px;height:32px;margin:-6px 0}#v-fonts .n{white-space:normal}.btn:disabled{opacity:.35}
.sh{display:flex;align-items:center;gap:8px;font-size:19px;margin:0}.tag{border:1.5px solid #000;border-radius:10px;padding:2px 8px;font-size:12px;font-weight:600}
.sd{font-size:13px;line-height:1.4;margin:10px 0 0}.sec{margin-bottom:22px}.sec .sd+label{margin-top:10px}
textarea{display:block;width:100%;height:160px;font:14px/1.35 JBM,ui-monospace,monospace;padding:12px;border:2px solid #000;border-radius:8px;resize:vertical;color:#000}::placeholder{color:#999}
.up{display:flex;gap:10px;align-items:center;margin-top:6px}.up button{flex:none;height:44px;padding:0 14px;border:2px solid #000;border-radius:8px;background:#fff;font-size:15px;font-weight:600}.up small{font-size:12px;line-height:1.4}
.foot{font-size:13px;line-height:1.4;margin:0}
.tg{display:flex;align-items:center;gap:12px;margin-top:10px}.tg>div,.tg>label{flex:1}.tg label{margin:0}.tg .sd{margin:4px 0 0}
input.sw{-webkit-appearance:none;appearance:none;flex:none;position:relative;width:51px;height:29px;padding:0;border:2px solid #000;border-radius:15px;background:#fff}
input.sw:before{content:'';position:absolute;top:3px;left:3px;width:19px;height:19px;border-radius:50%;background:#000;transition:left .15s}
input.sw:checked{background:#000}input.sw:checked:before{left:25px;background:#fff}input.sw:disabled{opacity:.35}
.r .n small{display:block;font-size:14px;margin-top:3px;overflow:hidden;text-overflow:ellipsis}.r .n b{font-weight:600}
.add{display:flex;align-items:center;justify-content:center;gap:8px;width:100%;padding:18px;margin-top:14px;border:2px dashed #000;border-radius:8px;background:#fff;font-size:16px;font-weight:600}.add b{font-size:20px}
.ps{display:flex;align-items:center;gap:8px;margin-top:14px;border:2px solid #000;border-radius:8px;padding:10px 10px 10px 14px}.ps b{flex:1;font-weight:600;overflow-wrap:anywhere}
.ib{flex:none;width:40px;height:40px;border:2px solid #000;border-radius:8px;background:#fff;display:flex;align-items:center;justify-content:center;padding:0}
.pf{margin-top:14px;border:2px solid #000;border-radius:12px;padding:16px}
.adv summary{display:flex;justify-content:space-between;align-items:center;font-size:14px;font-weight:600;margin-top:14px;cursor:pointer;list-style:none}.adv summary::-webkit-details-marker{display:none}
.adv summary:after{content:'›';font-size:22px;font-weight:400;line-height:1}.adv[open] summary:after{transform:rotate(90deg)}.adv .gr{opacity:.4}
.lb{display:flex;align-items:center;gap:6px;position:relative;margin:10px 0 6px}.lb label{margin:0}.tg .lb{margin:0}
.ii{flex:none;width:24px;height:24px;padding:0;border:0;background:none;color:#000;display:flex;align-items:center;justify-content:center}
.pop{position:absolute;left:0;top:100%;margin-top:6px;width:260px;max-width:calc(100vw - 72px);background:#fff;border:2px solid #000;border-radius:8px;padding:10px 12px;font-size:13px;font-weight:400;line-height:1.4;z-index:3;box-shadow:0 4px 12px rgba(0,0,0,.25)}
select{width:100%;height:47px;font:inherit;padding:0 40px 0 14px;border:2px solid #000;border-radius:8px;color:#000;-webkit-appearance:none;appearance:none;background:#fff url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='20' height='20' viewBox='0 0 24 24' fill='none' stroke='%23000' stroke-width='2.5' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='m6 9 6 6 6-6'/%3E%3C/svg%3E") no-repeat right 12px center}.two{display:flex;gap:12px;margin-top:14px}
.two button{flex:1;height:50px;border:2px solid #000;border-radius:10px;background:#fff;font-size:17px;font-weight:700}.two .k{background:#000;color:#fff}
.al .ic{display:flex;justify-content:center;margin:20px 0 -10px}.spin{animation:sp 1s linear infinite}@keyframes sp{to{transform:rotate(360deg)}}.up .sv{font-weight:600}
.al button{flex:1;height:48px;background:#fff;border:0;font-size:17px}.al button+button{border-left:2px solid #000;font-weight:700}.al button:only-child{font-weight:700}
.r .ico{flex:none}.ckb{flex:none;width:26px;height:26px;border:2px solid #000;border-radius:5px;background:#fff;color:#fff;padding:0;display:flex;align-items:center;justify-content:center}.ckb.on{background:#000}
.r .t{flex:1;min-width:0}.r .t b{display:block;font-weight:600;overflow-wrap:anywhere}.r .t b.dn{font-weight:400;text-decoration:line-through}.r .t small{display:block;font-size:13px;margin-top:2px}.r .t .pr{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.dt{display:flex;align-items:center;gap:12px;padding:12px 14px;border-top:1px solid #000}.dt:first-child{border-top:0}.dt .t{flex:1;cursor:pointer}.dt .t b{display:block;font-weight:600}.dt .t small{display:block;font-size:13px;margin-top:2px}
.cal{padding:4px 14px 14px}.cal .mh{display:flex;align-items:center;gap:4px;margin-bottom:4px}.cal .mh b{flex:1}.cal .mh button{width:36px;height:36px;border:0;background:none;font-size:24px;padding:0}
.cal .gd{display:grid;grid-template-columns:repeat(7,1fr);gap:2px;text-align:center}.cal .wd{font-size:12px;color:#888;padding:6px 0}
.cal .gd button{height:38px;border:0;border-radius:19px;background:none;font-size:15px;padding:0}.cal .gd .td{box-shadow:inset 0 0 0 1.5px #000}.cal .gd .sel{background:#000;color:#fff;font-weight:700}
.wh{position:relative;display:flex;justify-content:center;gap:4px;padding:0 14px 14px}.wh .band{position:absolute;left:14px;right:14px;top:72px;height:36px;background:#e6e6e6;border-radius:8px}
.wh .col{position:relative;width:76px;height:180px;overflow-y:scroll;scroll-snap-type:y mandatory;scrollbar-width:none}.wh .col::-webkit-scrollbar{display:none}.wh .col i{display:block;height:72px}
.wh .col div{height:36px;line-height:36px;text-align:center;scroll-snap-align:center;font-size:20px;cursor:pointer}.wh .co{position:relative;line-height:180px;font-weight:700;font-size:20px}
.nv h2{font-size:22px;margin:0 0 4px}.nv .cd{font-size:13px;text-align:right;margin:0 0 12px}.nv .bd{white-space:pre-wrap;overflow-wrap:anywhere;font-size:16px;line-height:1.5;margin:0 0 8px}
.emp{text-align:center;padding:40px 0}.emp b{display:block;font-size:18px;margin-bottom:8px}.emp p{font-size:14px;margin:0}.cr{font-size:13px;margin:12px 0 0}
</style></head><body><main>
<header><div><h1 id="ttl">HeySticky setup</h1><p id="sub">Connected to your Sticky · 192.168.4.1</p></div><button class="back" id="back" hidden aria-label="Back">‹</button></header>
<div class="c" id="v-setup"><div class="warn" id="warn" hidden><svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="9.5"/><path d="M12 11v6"/><circle cx="12" cy="7.5" r=".6" fill="currentColor"/></svg><div><b id="warn-t"></b><p id="warn-p"></p></div></div><div class="g">
<button class="r" id="rp"><svg class="ico" viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="8" r="4"/><path d="M4 21v-1a6 6 0 0 1 6-6h4a6 6 0 0 1 6 6v1"/></svg><b class="n" id="lp">Profile</b><span id="pv"></span><span class="ch">›</span></button>
<button class="r" id="rw"><svg class="ico" viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12.55a11 11 0 0 1 14 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><circle cx="12" cy="20" r="1"/></svg><b class="n">Wi-Fi</b><span id="wv"></span><span class="ch">›</span></button>
<button class="r" id="rf"><svg class="ico" viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20V3H6.5A2.5 2.5 0 0 0 4 5.5z"/><path d="M4 19.5A2.5 2.5 0 0 0 6.5 22H20v-5"/></svg><b class="n" id="lf">Font book</b><span class="ch">›</span></button>
<button class="r" id="ri"><svg class="ico" viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 22v-5"/><path d="M9 8V2"/><path d="M15 8V2"/><path d="M18 8v5a4 4 0 0 1-4 4h-4a4 4 0 0 1-4-4V8z"/></svg><b class="n" id="li">Integrations</b><span class="ch">›</span></button>
<button class="r" id="rt"><svg class="ico" viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="18" height="18" rx="2"/><path d="m9 12 2 2 4-4"/></svg><b class="n" id="lt">To-do</b><span class="ch">›</span></button>
<button class="r" id="rn"><svg class="ico" viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6"/><path d="M16 13H8"/><path d="M16 17H8"/><path d="M10 9H8"/></svg><b class="n" id="ln">Notes</b><span class="ch">›</span></button></div><p class="foot" id="foot"></p></div>
<form class="c" id="v-profile" hidden>
<label for="name" id="lname">Your name</label><input id="name" maxlength="32">
<label for="city" id="lcity">City (for weather)</label><div class="city"><input id="city" maxlength="64" autocomplete="off"><div class="sug" id="sug" hidden></div></div>
<button class="btn k" id="save">✓&nbsp; Save to Sticky</button></form>
<div class="c" id="v-wifi" hidden></div>
<div class="c" id="v-det" hidden></div>
<div class="c" id="v-fonts" hidden></div>
<div class="c" id="v-addfont" hidden></div>
<div class="c" id="v-integ" hidden></div>
<div class="c" id="v-ai" hidden></div>
<div class="c" id="v-stt" hidden></div>
<div class="c" id="v-nai" hidden></div>
<div class="c" id="v-todo" hidden></div>
<div class="c" id="v-task" hidden></div>
<div class="c" id="v-notes" hidden></div>
<div class="c" id="v-note" hidden></div>
<div class="c" id="v-noteed" hidden></div>
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
const L=(e,v)=>S.lang==='vi'?v:e;  // English / Tiếng Việt, like TR() on the Sticky
let S={saved:[],scan:[]},view='setup',det='',pending=false,lat='',lon='',C={todo:-1,notes:-1},dirty=()=>false;
const api=(p,b)=>fetch(p,b?{method:'POST',body:new URLSearchParams(b)}:{});
const seen=s=>S.scan.find(n=>n.ssid===s);

async function load(){try{S=await(await api('/api/status')).json()}catch(e){return}
 if(pending&&S.join&&S.join.state!=='busy'){pending=false;
  if(S.join.state==='badpass')alertBox(L('Incorrect password for “','Sai mật khẩu cho “')+S.join.ssid+'”');
  else if(S.join.state==='fail')alertBox(L('Couldn\'t join “','Không kết nối được “')+S.join.ssid+'”')}
 render()}

function go(v,d){view=v;det=d||'';dirty=()=>false;
 if(v==='setup')counts();if(v==='todo')todoView();if(v==='task')taskView();if(v==='notes')notesView();if(v==='note')noteView();if(v==='noteed')noteEdView();
 if(v==='profile'){$('name').value=S.name||'';$('city').value=S.city||'';lat=lon='';$('sug').hidden=true;$('save').textContent=L('✓  Save to Sticky','✓  Lưu vào Sticky')}
 if(v==='fonts')fontsView();if(v==='addfont')addView();if(v==='integ')integView();if(v==='ai')aiView();if(v==='stt'||v==='nai')svcView(v);
 render();scrollTo(0,0)}
// Each page opened gets a history entry, so the phone's swipe back (and ‹, and "done" returns) all go up one page.
const UP={det:'wifi',addfont:'fonts',ai:'integ',stt:'ai',nai:'ai',task:'todo',note:'notes'};
function nav(v,d){history.pushState(null,'');go(v,d)}
// A form with unsaved changes asks first (06p2 / 06r2): the back step is undone until Discard.
addEventListener('popstate',()=>{if(dirty()){history.pushState(null,'');alertBox(L('Discard changes?','Bỏ thay đổi?'),L('Your changes won’t be saved.','Thay đổi sẽ không được lưu.'),L('Discard','Bỏ'),()=>{dirty=()=>false;history.back()});return}
 close();if(view==='noteed'&&det!=='new')go('note',det);else go(UP[view]||'setup')});
$('back').onclick=()=>history.back();
$('rt').onclick=()=>nav('todo');$('rn').onclick=()=>nav('notes');$('rp').onclick=()=>nav('profile');$('rw').onclick=()=>nav('wifi');$('rf').onclick=()=>nav('fonts');$('ri').onclick=()=>nav('integ');

function row(n,o){const joining=S.join&&S.join.state==='busy'&&S.join.ssid===n.ssid;
 const r=E('div',{className:'r'}),ck=E('span',{className:'ck'});if(o.cur)ck.append(check());
 const nm=E(o.cur?'b':'span',{className:'n',textContent:n.ssid});r.append(ck,nm);
 if(joining)r.append(E('span',{textContent:L('Joining…','Đang kết nối…')}));
 else{if(n.enc)r.append(lock());if(n.rssi!=null)r.append(wifi(lvl(n.rssi)));
  if(o.info){const b=E('button',{className:'i',ariaLabel:'Details'},info());b.onclick=e=>{e.stopPropagation();nav('det',n.ssid)};r.append(b)}}
 if(o.tap)r.onclick=o.tap;return r}

function render(){
 const cur=S.cur?S.cur.ssid:'';
 for(const v of['setup','profile','wifi','det','fonts','addfont','integ','ai','stt','nai','todo','task','notes','note','noteed'])$('v-'+v).hidden=v!==view;
 $('back').hidden=view==='setup';$('sub').hidden=view!=='setup';
 $('ttl').textContent={setup:L('HeySticky setup','Cài đặt HeySticky'),profile:L('Profile','Hồ sơ'),wifi:'Wi-Fi',det,fonts:L('Font book','Sổ tay phông chữ'),addfont:L('Add font','Thêm phông'),integ:L('Integrations','Tích hợp'),ai:'AI',stt:SVC.stt(),nai:SVC.nai(),
  todo:L('To-do','Việc cần làm'),task:det==='new'?L('New task','Việc mới'):L('Edit task','Sửa việc'),notes:L('Notes','Ghi chú'),note:L('Note','Ghi chú'),noteed:det==='new'?L('New note','Ghi chú mới'):L('Edit note','Sửa ghi chú')}[view];
 const t=$('ttl');let z=26;t.style.fontSize='';while(t.scrollWidth>t.clientWidth&&z>16)t.style.fontSize=--z+'px';  // long titles shrink to fit
 $('sub').textContent=L('Connected to your Sticky · ','Đã kết nối với Sticky · ')+location.hostname;
 $('warn').hidden=!S.home;$('warn-t').textContent=L('Open on your home Wi-Fi','Đang mở trên Wi-Fi nhà');
 $('warn-p').textContent=L('Anyone on this Wi-Fi can open this page, and it isn’t encrypted. Use it only on a Wi-Fi you trust.','Ai dùng chung Wi-Fi này đều mở được trang này, và trang không được mã hoá. Chỉ dùng trên Wi-Fi bạn tin cậy.');
 const cnt=n=>n>=0?' ('+n+')':'';$('lt').textContent=L('To-do','Việc cần làm')+cnt(C.todo);$('ln').textContent=L('Notes','Ghi chú')+cnt(C.notes);
 $('lp').textContent=L('Profile','Hồ sơ');$('lf').textContent=L('Font book','Sổ tay phông chữ');$('li').textContent=L('Integrations','Tích hợp');
 $('foot').textContent=L('Keys are stored only on this Sticky. They are never part of the firmware file you flash or share.','Khoá chỉ được lưu trên Sticky này, không bao giờ nằm trong file firmware bạn nạp hay chia sẻ.');
 $('lname').textContent=L('Your name','Tên của bạn');$('lcity').textContent=L('City (for weather)','Thành phố (cho thời tiết)');
 $('pv').textContent=S.name||'';$('wv').textContent=S.wifi_on===false?L('Off','Tắt'):cur||L('Not connected','Chưa kết nối');
 if(view==='wifi'){const w=$('v-wifi');w.replaceChildren();
  const net=s=>Object.assign({ssid:s},seen(s)||{});
  const on=S.wifi_on!==false,sw=E('input',{type:'checkbox',className:'sw',id:'w-on',checked:on});  // 06b switch, saved as it flips
  sw.onchange=async()=>{S.wifi_on=sw.checked;render();try{if(!(await api('/api/wifi_on',{on:sw.checked?1:0})).ok)throw 0}catch(e){alertBox(L('Couldn\'t save','Không lưu được'),'')}load()};
  w.append(E('div',{className:'g'},E('div',{className:'r'},E('label',{htmlFor:'w-on',className:'n',style:'margin:0;font-size:16px',textContent:'Wi-Fi'}),sw)));
  if(!on){w.append(E('p',{className:'note'},L('The Sticky won\'t join any network. Weather, clock sync, AI and updates need Wi\u2060-\u2060Fi.','Sticky sẽ không kết nối mạng nào. Thời tiết, đồng bộ giờ, AI và cập nhật cần Wi\u2060-\u2060Fi.')));return}
  if(cur)w.append(E('div',{className:'g'},row(Object.assign(net(cur),{rssi:S.cur.rssi}),{cur:1,info:1})));
  const my=S.saved.filter(s=>s!==cur);
  if(my.length)w.append(E('h2',{textContent:L('MY NETWORKS','MẠNG CỦA TÔI')}),E('div',{className:'g'},...my.map(s=>row(net(s),{info:1,tap:()=>join(s,'')}))));
  const other=S.scan.filter(n=>n.ssid!==cur&&!S.saved.includes(n.ssid));
  const o=E('div',{className:'r'},E('span',{className:'ck'}),E('span',{className:'n',textContent:L('Other…','Khác…')}));o.onclick=()=>sheet('');
  w.append(E('h2',{textContent:L('OTHER NETWORKS','MẠNG KHÁC')}),E('div',{className:'g'},...other.map(n=>row(n,{tap:()=>n.enc?sheet(n.ssid):join(n.ssid,'')})),o));
  const rf=E('button',{textContent:S.scanning?L('Looking…','Đang tìm…'):L('Refresh','Làm mới')});rf.onclick=async()=>{try{await api('/api/scan',{})}catch(e){}load()};
  w.append(E('p',{className:'note'},L('Found when Web setup was turned on. ','Tìm thấy lúc bật Cài đặt qua web. '),rf))}
 if(view==='det'){const d=$('v-det');d.replaceChildren();const n=seen(det),on=det===cur;
  const kv=(k,v)=>E('div',{className:'r'},E('b',{className:'n',textContent:k}),E('span',{textContent:v}));
  const rs=on?S.cur.rssi:n?n.rssi:null;
  const g=E('div',{className:'g'},kv(L('Status','Trạng thái'),on?L('Connected','Đã kết nối'):L('Saved','Đã lưu')),kv(L('Signal','Tín hiệu'),rs==null?L('Not in range','Ngoài vùng phủ'):L(['','Weak','Good','Excellent'],['','Yếu','Tốt','Rất tốt'])[lvl(rs)]));
  if(n)g.append(kv(L('Security','Bảo mật'),ENC[n.enc]||L('Secured','Có bảo mật')));if(on)g.append(kv(L('IP address','Địa chỉ IP'),S.cur.ip));
  const f=E('button',{className:'btn',textContent:L('Forget This Network','Quên mạng này')});
  f.onclick=()=>alertBox(L('Forget “','Quên “')+det+'”?',L('Your Sticky will stop joining it.','Sticky sẽ không tự kết nối mạng này nữa.'),L('Forget','Quên'),async()=>{try{await api('/api/forget',{ssid:det})}catch(e){}history.back();load()});
  d.append(g,f)}}

async function join(ssid,pass){pending=true;S.join={ssid,state:'busy'};render();
 try{await api('/api/join',{ssid,pass})}catch(e){}  // the hotspot can drop for a moment while the Sticky changes channel
 }

function close(){$('ov').replaceChildren()}
function sheet(ssid){const pw=E('input',{type:'password',placeholder:L('Password','Mật khẩu'),maxLength:63,autocomplete:'off'});
 const nm=E('input',{placeholder:L('Network name','Tên mạng'),maxLength:32,autocomplete:'off',autocapitalize:'off'});
 const x=E('button',{className:'rb',textContent:'✕',ariaLabel:'Cancel'}),ok=E('button',{className:'rb k',textContent:'✓',ariaLabel:'Join'});
 const sub=()=>{const s=ssid||nm.value.trim();if(!s)return;close();join(s,pw.value)};
 x.onclick=close;ok.onclick=sub;pw.onkeydown=nm.onkeydown=e=>{if(e.key==='Enter')sub()};
 const sh=E('div',{className:'sheet'},E('div',{className:'bar'},x,ok),E('div',{className:'big'},wifi(3,88)),
  E('h3',{textContent:ssid?L('Join “','Kết nối “')+ssid+'”':L('Join another network','Kết nối mạng khác')}),
  E('p',{textContent:ssid?L('Enter the password to join this Wi-Fi network.','Nhập mật khẩu để kết nối mạng Wi-Fi này.'):L('Enter the name and password of the Wi-Fi network.','Nhập tên và mật khẩu của mạng Wi-Fi.')}));
 if(!ssid)sh.append(nm);sh.append(pw);
 $('ov').replaceChildren(E('div',{className:'dim',onclick:close}),sh);(ssid?pw:nm).focus()}
function alertBox(title,text,yes,fn){const b=E('div'),a=E('div',{className:'al'},E('h3',{textContent:title}));
 if(text)a.append(E('p',{textContent:text}));else a.firstChild.style.marginBottom='18px';
 if(yes){b.append(E('button',{textContent:L('Cancel','Huỷ'),onclick:close}),E('button',{textContent:yes,onclick:()=>{close();fn()}}))}
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
  b.textContent=L('✓  Saved','✓  Đã lưu');await load();setTimeout(()=>history.back(),900)}
 catch(e){alertBox(L('Couldn\'t save','Không lưu được'))}};

// ---------- Font book (06m) ----------
let F={cur:'',list:[],sd:true};
async function fontsView(){const v=$('v-fonts');try{F=await(await api('/api/fonts')).json()}catch(e){}
 const tag=t=>E('span',{textContent:t}),rows=[E('div',{className:'r'},E('b',{className:'n',textContent:L('JetBrains Mono (built-in)','JetBrains Mono (có sẵn)')}),tag(F.cur?'':L('In use','Đang dùng')))];
 for(const n of F.list){const d=E('button',{className:'ib',ariaLabel:L('Delete','Xoá')},H(svg(20,'<path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/>')));
  d.onclick=()=>alertBox(L('Delete “','Xoá “')+n+'”?',L('It is removed from the SD card.','Phông sẽ bị xoá khỏi thẻ SD.'),L('Delete','Xoá'),async()=>{try{await api('/api/font_del',{name:n})}catch(e){}fontsView();load()});
  rows.push(E('div',{className:'r'},E('b',{className:'n',textContent:n}),n===F.cur?tag(L('In use','Đang dùng')):'',d))}
 const add=E('button',{className:'btn k',textContent:L('+  Add font','+  Thêm phông'),disabled:!F.sd||F.list.length>=8});add.onclick=()=>nav('addfont');
 v.replaceChildren(E('div',{className:'g'},...rows),add,E('p',{className:'note',style:'margin-top:12px',textContent:!F.sd?L('Put a microSD card in the Sticky first.','Hãy lắp thẻ microSD vào Sticky trước.'):
  L('Pick the font on the Sticky: Settings › Language & Font › User Font.','Chọn phông trên Sticky: Cài đặt › Ngôn ngữ & Phông › Phông chữ.')}))}

// ---------- Add font (06n) ----------
const NB=' ',CH=(()=>{let s='đĐ'+NB+'°·…“”‘’–—•●×−';for(let c=0x21;c<0x7f;c++)s+=String.fromCharCode(c);  // = tools/make_fonts.py TEXT
 for(const b of'aăâeêioôơuưyAĂÂEÊIOÔƠUƯY')for(const t of['','̀','́','̉','̃','̣'])s+=(b+t).normalize('NFC');
 return[...new Set(s)].sort((a,b)=>a.codePointAt(0)-b.codePointAt(0))})();
const STY=[['r','Regular','Thường'],['b','Bold','Đậm'],['x','ExtraBold','Rất đậm'],['i','Italic','Nghiêng']];
// Sticky font slots (userfont.h): style letter + px. Digit-only fonts stay built-in.
const SLOTS=[['R20','r',20],['R25','r',25],['R30','r',30],['B20','b',20],['B25','b',25],['B26','b',26],['B28','b',28],['B35','b',35],['X25','x',25],['X30','x',30],['X32','x',32],['X40','x',40],['I25','i',25]];
let FF={};  // style -> {file, fam}
const fam=k=>FF[k]?FF[k].fam:FF.r.fam;  // a missing style falls back to Regular
const cx=document.createElement('canvas').getContext('2d',{willReadFrequently:true});
function has(f,ch){cx.font='40px "'+f+'",monospace';const a=cx.measureText(ch).width;cx.font='40px "'+f+'",serif';return a===cx.measureText(ch).width}
function ink(c,g,w,h){const d=g.getImageData(0,0,w,h),a=d.data,o=new Uint8Array(w*h);  // 1-bit, like the panel
 for(let i=0;i<w*h;i++){o[i]=a[i*4+3]>=128?255:0;a[i*4]=a[i*4+1]=a[i*4+2]=0;a[i*4+3]=o[i]}g.putImageData(d,0,0);return o}
function vlw(f,px){const c=document.createElement('canvas'),g=c.getContext('2d',{willReadFrequently:true}),font=px+'px "'+f+'"';
 g.font=font;const m=g.measureText('Hg'),rec=[],bm=[];
 for(const ch of CH){if(ch!==NB&&!has(f,ch))continue;g.font=font;const t=g.measureText(ch===NB?' ':ch),adv=Math.round(t.width);
  if(ch===NB){rec.push([0xa0,0,0,adv,0,0,0]);continue}
  const x0=Math.floor(-t.actualBoundingBoxLeft),y0=Math.floor(-t.actualBoundingBoxAscent);
  const w=Math.max(Math.ceil(t.actualBoundingBoxRight)-x0,0),h=Math.max(Math.ceil(t.actualBoundingBoxDescent)-y0,0);
  if(w>255||h>255||adv>255)throw new Error(ch);
  let b=new Uint8Array(0);if(w&&h){c.width=w;c.height=h;g.font=font;g.fillText(ch,-x0,-y0);b=ink(c,g,w,h)}
  rec.push([ch.codePointAt(0),h,w,adv,-y0,Math.max(-128,Math.min(127,x0)),0]);bm.push(b)}
 const u=new Uint8Array(24+rec.length*28+bm.reduce((n,b)=>n+b.length,0)),dv=new DataView(u.buffer);let o=0;
 for(const v of[rec.length,11,px,0,Math.round(m.fontBoundingBoxAscent),Math.round(m.fontBoundingBoxDescent)].concat(...rec)){dv.setInt32(o,v);o+=4}
 for(const b of bm){u.set(b,o);o+=b.length}return u}
function sample(f,px,text){const c=document.createElement('canvas'),g=c.getContext('2d',{willReadFrequently:true}),font=px+'px "'+f+'"';
 g.font=font;const m=g.measureText(text);c.width=Math.ceil(m.width)+2;c.height=Math.ceil(m.fontBoundingBoxAscent+m.fontBoundingBoxDescent);
 g.font=font;g.fillText(text,0,Math.round(m.fontBoundingBoxAscent));ink(c,g,c.width,c.height);return c}

function addView(){FF={};const v=$('v-addfont'),nm=E('input',{id:'fname',maxLength:24,autocomplete:'off',autocapitalize:'off'});
 const warn=E('p',{className:'warn',hidden:true}),pv=E('div',{className:'pv',hidden:true}),msg=E('p',{className:'note',style:'margin-top:12px'});
 const upb=E('button',{className:'btn k',textContent:L('✓  Upload to Sticky','✓  Tải lên Sticky'),disabled:true});
 const okName=()=>/^[A-Za-z0-9_.-][A-Za-z0-9 _.-]{0,23}$/.test(nm.value);
 const rows=STY.map(([k,en,vi])=>{const val=E('span',{textContent:L('Choose file…','Chọn tệp…')}),inp=E('input',{type:'file',accept:'.ttf,.otf',hidden:true});
  const r=E('div',{className:'r',role:'button',tabIndex:0},E('b',{className:'n',textContent:L(en,vi)+(k==='r'?L(' (required)',' (bắt buộc)'):'')}),val,inp);
  r.onclick=()=>inp.click();inp.onclick=e=>e.stopPropagation();
  inp.onchange=async()=>{const f=inp.files[0];if(!f)return;val.textContent=f.name;
   try{const fm='hs-'+k+'-'+Date.now(),face=new FontFace(fm,await f.arrayBuffer());await face.load();document.fonts.add(face);FF[k]={file:f.name,fam:fm}}
   catch(e){delete FF[k];val.textContent=L('Not a font file','Không phải tệp phông')}
   if(k==='r'&&FF.r&&!nm.value)nm.value=f.name.replace(/\.[^.]*$/,'').replace(/[-_ ]?Regular$/i,'').replace(/[^A-Za-z0-9 _.-]/g,'').slice(0,24);
   show()};return r});
 function show(){upb.disabled=!FF.r||!okName();if(!FF.r){warn.hidden=pv.hidden=true;return}
  const miss=STY.filter(([k])=>!FF[k]).map(s=>L(s[1],s[2])),noVi=!has(FF.r.fam,'ệ');
  warn.textContent=[miss.length?L('⚠  No '+miss.join(' or ')+' file. Text in those styles will use Regular.','⚠  Thiếu tệp '+miss.join(' và ')+'. Chữ kiểu đó sẽ dùng kiểu Thường.'):'',
   noVi?L('⚠  This font has no Vietnamese letters; they will be missing.','⚠  Phông này không có chữ tiếng Việt; các chữ đó sẽ bị thiếu.'):''].filter(Boolean).join(' ');
  warn.hidden=!warn.textContent;const who=S.name||'David';
  const ex=[['r',25,L('Hello, ','Xin chào, ')+who+' · 27.4°C'],['b',28,L('Settings · To-do','Cài đặt · Việc cần làm')],['x',32,L('Software Update','Cập nhật phần mềm')],['i',25,L('Start typing…','Bắt đầu gõ…')]];
  pv.replaceChildren(...ex.flatMap(([k,px,t])=>{const s=STY.find(x=>x[0]===k);
   return[E('small',{textContent:L(s[1],s[2])+' · '+px+' px'+(FF[k]?'':'  →  '+L('Regular','Thường'))}),sample(fam(k),px,t)]}));pv.hidden=false}
 nm.oninput=show;
 upb.onclick=async()=>{const name=nm.value;upb.disabled=true;
  try{for(let i=0;i<SLOTS.length;i++){const[slot,k,px]=SLOTS[i];msg.textContent=L('Uploading ','Đang tải lên ')+(i+1)+'/'+SLOTS.length+'…';
    const fd=new FormData();fd.append('f',new Blob([vlw(fam(k),px)]),slot+'.vlw');
    const r=await fetch('/api/font?name='+encodeURIComponent(name)+'&slot='+slot,{method:'POST',body:fd});if(!r.ok)throw 0}
   msg.textContent='';alertBox(L('“'+name+'” is on the Sticky','Đã lưu “'+name+'” vào Sticky'),L('Pick it in Settings › Language & Font › User Font.','Chọn trong Cài đặt › Ngôn ngữ & Phông › Phông chữ.'));history.back()}
  catch(e){msg.textContent='';upb.disabled=false;alertBox(L('Couldn\'t upload the font','Không tải lên được phông'))}};
 v.replaceChildren(E('label',{htmlFor:'fname',textContent:L('Font name','Tên phông')}),nm,E('h2',{style:'margin-top:22px',textContent:L('Font files','Tệp phông')}),E('div',{className:'g'},...rows),warn,
  E('h2',{textContent:L('Preview — as on the Sticky','Xem trước — như trên Sticky')}),pv,
  E('p',{className:'note',style:'margin:0',textContent:L('Numbers on Home and the clock keep the built-in font.','Số trên Trang chủ và đồng hồ vẫn dùng phông có sẵn.')}),upb,msg)}
// ---------- Integrations (06g) + AI (06h). Notion, Home Assistant and Remote access come in Phase 3. ----------
function integView(){const r=E('button',{className:'r'},E('b',{className:'n',textContent:'AI'}),E('span',{className:'ch',textContent:'›'}));
 r.onclick=()=>nav('ai');$('v-integ').replaceChildren(E('div',{className:'g'},r))}
let A={};
async function aiLoad(){try{A=await(await api('/api/ai')).json();return true}catch(e){alertBox(L('Couldn\'t reach the Sticky','Không kết nối được Sticky'));return false}}
const SVC={stt:()=>L('Speech-to-text','Giọng nói thành chữ'),nai:()=>L('AI for notes & to-dos','AI cho ghi chú & việc cần làm')};
async function aiView(){const v=$('v-ai');v.replaceChildren();if(!await aiLoad())return;
 const r=(id,set,m)=>{const b=E('button',{className:'r'},E('span',{className:'n'},E('b',{textContent:SVC[id]()}),E('small',{textContent:set?m.split('/').pop():L('Not set','Chưa cài đặt')})),E('span',{className:'ch',textContent:'›'}));
  b.onclick=()=>nav(id);return b};
 v.append(E('div',{className:'g'},r('stt',A.stt_set,A.stt_model),r('nai',A.ai_set,A.ai_model)))}
// 06h3: a pop-up with an icon; no ok = no button (Testing…), ok = an OK button that runs it (or just closes)
function popBox(ic,title,text,ok){const a=E('div',{className:'al'},E('span',{className:'ic'},ic),E('h3',{textContent:title}),E('p',{textContent:text}));
 if(ok!==undefined)a.append(E('div',{},E('button',{textContent:'OK',onclick:()=>{close();if(ok)ok()}})));
 $('ov').replaceChildren(E('div',{className:'dim'}),a)}
const host=u=>{try{return new URL(u).host}catch(e){return u}};
const failText=(R,u)=>R.why==='stt'?L('Add speech-to-text first, or fill in the Base URL and API key.','Hãy thêm giọng nói thành chữ trước, hoặc điền URL gốc và khoá API.')
 :R.why==='wifi'?L('The Sticky isn\'t on Wi-Fi. Join a network first.','Sticky chưa kết nối Wi-Fi. Hãy kết nối một mạng trước.')
 :R.code===-9?L('Couldn\'t reach the Sticky.','Không kết nối được Sticky.')
 :R.code<0?L('Couldn\'t reach ','Không kết nối được tới ')+host(u)+'.'
 :R.code===401||R.code===403?L('Wrong API key. Check it and try again.','Sai khoá API. Hãy kiểm tra rồi thử lại.')
 :R.code===400||R.code===404||R.code===422?L('Check the Base URL and model.','Hãy kiểm tra URL gốc và mô hình.')+(R.msg?' ('+R.msg+')':'')
 :L('The service answered ','Dịch vụ trả lời ')+R.code+(R.msg?': '+R.msg:'.');
// A field label; tip = an (i) that opens a small pop-up on top (any tap elsewhere closes it)
const lab=(id,t,tip)=>{const l=E('label',{htmlFor:id,textContent:t});if(!tip)return l;
 const p=E('div',{className:'pop',hidden:true,textContent:tip}),b=E('button',{type:'button',className:'ii',ariaLabel:'Info'},H(svg(18,'<circle cx="12" cy="12" r="10" stroke-width="2"/><path d="M12 11v6"/><circle cx="12" cy="7.5" r=".4"/>')));
 b.onclick=()=>p.hidden=!p.hidden;return E('div',{className:'lb'},l,b,p)};
document.addEventListener('click',e=>{for(const p of document.querySelectorAll('.pop'))if(!p.parentNode.contains(e.target))p.hidden=true});
// 06h1 / 06h2: dashed "Add model provider" -> form (Cancel / Save = test, then save) -> saved line with edit / delete
async function svcView(id){const v=$('v-'+id);v.replaceChildren();if(!await aiLoad())return;
 const stt=id==='stt',svc=stt?'stt':'ai',p=svc+'_',slot=E('div');
 v.append(E('div',{className:'sec'},E('span',{className:'tag',textContent:L('OPENAI-COMPATIBLE','TƯƠNG THÍCH OPENAI')}),
  E('p',{className:'sd',textContent:stt?L('Turns your voice into text. Any service with an OpenAI-style /audio/transcriptions endpoint.','Chuyển giọng nói thành chữ. Dùng được mọi dịch vụ có endpoint /audio/transcriptions kiểu OpenAI.')
   :L('Writes notes using your instructions and splits to-dos. Works with OpenRouter or any OpenAI-style /chat/completions API.','Viết ghi chú theo hướng dẫn của bạn và tách việc cần làm. Dùng được với OpenRouter hoặc mọi API /chat/completions kiểu OpenAI.')}),slot));
 const show=()=>{if(!A[p+'set']){const b=E('button',{className:'add'},E('b',{textContent:'+'}),L('Add model provider','Thêm nhà cung cấp mô hình'));b.onclick=form;slot.replaceChildren(b);return}
  const ed=E('button',{className:'ib',ariaLabel:L('Edit','Sửa')},H(svg(20,'<path d="M4 20h4L19 9l-4-4L4 16v4zM13.5 6.5l4 4"/>')));
  const del=E('button',{className:'ib',ariaLabel:L('Delete','Xoá')},H(svg(20,'<path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/>')));
  ed.onclick=form;
  del.onclick=()=>alertBox(stt?L('Delete speech-to-text?','Xoá giọng nói thành chữ?'):L('Delete AI for notes & to-dos?','Xoá AI cho ghi chú & việc cần làm?'),
   L('The API key will be removed from the Sticky.','Khoá API sẽ bị xoá khỏi Sticky.'),L('Delete','Xoá'),async()=>{try{await api('/api/ai_del',{svc})}catch(e){}if(await aiLoad())show()});
  slot.replaceChildren(E('div',{className:'ps'},E('b',{textContent:A[p+'model']}),ed,del))};
 function form(){const inp={},f=(k,t,val,ph,pw,tip)=>{inp[k]=E('input',{id:'a-'+k,value:val||'',placeholder:ph||'',type:pw?'password':'text',autocomplete:'off',autocapitalize:'off',spellcheck:false,maxLength:k==='order'?100:200});
   return[lab('a-'+k,t,tip),inp[k]]};
  const save=E('button',{type:'button',className:'k',textContent:L('Save','Lưu')});
  // OpenRouter only (hidden for other APIs), Figma 06h1 / 06h2: Advanced › = 06h2 Mode, then Provider (on = hosts tried first, Mode greyed)
  const psw=E('input',{type:'checkbox',className:'sw',id:'a-psw',checked:!!A[p+'order']}),route=E('details',{className:'adv',open:!!A[p+'order']},E('summary',{textContent:L('Advanced','Nâng cao')}));
  let mode=E('div');
  if(!stt){inp.sort=E('select',{id:'a-sort'},...[['',L('Balanced (price + speed)','Cân bằng (giá + tốc độ)')],['throughput',L('Fastest (Nitro)','Nhanh nhất (Nitro)')],
    ['price',L('Cheapest (Floor)','Rẻ nhất (Floor)')]].map(([v,t])=>E('option',{value:v,textContent:t})));inp.sort.value=A.ai_sort||'';
   mode=E('div',{},lab('a-sort',L('Mode','Chế độ'),L('OpenRouter picks the host for you.','OpenRouter tự chọn nơi chạy mô hình.')),inp.sort);route.append(mode)}
  inp.order=E('input',{id:'a-order',value:A[p+'order']||'',placeholder:L('Any (e.g. groq, deepinfra)','Bất kỳ (vd. groq, deepinfra)'),autocomplete:'off',autocapitalize:'off',spellcheck:false,maxLength:100});
  route.append(E('div',{className:'tg'},E('div',{},lab('a-psw',L('Provider','Nhà cung cấp'),L('Auto-fallback to other provider if it is down','Tự chuyển sang nhà cung cấp khác nếu nó lỗi'))),psw),inp.order);
  const sync=()=>{inp.order.hidden=!psw.checked;mode.classList.toggle('gr',psw.checked);if(inp.sort)inp.sort.disabled=psw.checked};psw.onchange=sync;sync();
  // 06h1 / 06h2 placeholders, Figma 06h: 06h2 left empty still uses speech-to-text's URL / key and the default model
  slot.replaceChildren(E('div',{className:'pf'},...f('url',L('Base URL','URL gốc'),A[p+'url'],'https://gateway.example/v1'),
   ...f('model',L('Model','Mô hình'),stt||A.ai_set?A[p+'model']:'',L('Model ID','ID mô hình')),
   ...f('key',L('API key','Khoá API'),'',A[p+'key']||L('Enter your API key','Nhập khoá API'),1),route,
   E('div',{className:'two'},E('button',{type:'button',textContent:L('Cancel','Huỷ'),onclick:show}),save)));
  const orx=()=>route.hidden=!/openrouter\.ai/.test(inp.url.value.trim()||A.stt_url||'');inp.url.oninput=orx;orx();
  save.onclick=async()=>{const b={svc};for(const k in inp)b[k]=inp[k].value.trim();if(!psw.checked)b.order='';
   if(stt&&(!b.url||!b.model))return alertBox(L('Fill in the Base URL and model','Hãy điền URL gốc và mô hình'));
   if(stt&&!b.key&&!A.stt_set)return alertBox(L('Paste your API key','Dán khoá API'));
   const u=b.url||A.stt_url,sp=H(svg(36,'<circle cx="12" cy="12" r="9" opacity=".2"/><path d="M21 12a9 9 0 0 0-9-9"/>'));sp.classList.add('spin');
   popBox(sp,L('Testing connection…','Đang kiểm tra kết nối…'),L('Sending a short test to ','Đang gửi một yêu cầu thử tới ')+host(u)+'.');
   let R;try{const r=await api('/api/ai_test',b);if(!r.ok)throw 0;R=await r.json()}catch(e){R={code:-9}}
   if(R.code===200){await aiLoad();popBox(H(svg(36,'<circle cx="12" cy="12" r="10" fill="currentColor" stroke="none"/><path d="M7.5 12.5l3 3 6-6.5" stroke="#fff"/>')),
    L('Connected','Đã kết nối'),(b.model||A.ai_def)+L(' is ready to use.',' đã sẵn sàng.'),show)}
   else popBox(H(svg(36,'<circle cx="12" cy="12" r="10" stroke-width="2"/><path d="M8.5 8.5l7 7M15.5 8.5l-7 7"/>')),L('Couldn\'t connect','Không kết nối được'),failText(R,u),null)}}
 show();if(stt)return;
 // 06h2 only: Plain text + Note instructions, saved as they change (toggles at once, the text when the box loses focus)
 const ta=E('textarea',{id:'a-notes',value:A.notes||'',maxLength:2000,placeholder:'# Title\n## Key points\n- …\n## Action items\n- [ ] …\n'+L('Keep it short. Write in English.','Viết ngắn gọn. Viết bằng tiếng Việt.')});
 const file=E('input',{type:'file',accept:'.txt,text/plain',hidden:true}),upb=E('button',{type:'button'},H(svg(18,'<path d="M12 15V4M7.5 8.5L12 4l4.5 4.5M5 15v4h14v-4"/>')),' '+L('Upload .txt','Tải lên .txt'));
 upb.style.display='flex';upb.style.alignItems='center';upb.style.gap='8px';upb.onclick=()=>file.click();
 const sw=(i,on)=>E('input',{type:'checkbox',className:'sw',id:i,checked:!!on}),plain=sw('a-plain',A.plain),useN=sw('a-instr',A.instr),ok=E('small',{className:'sv'});
 const nbox=E('div',{},E('p',{className:'sd',style:'margin:6px 0',textContent:L('Tell the AI how to write your notes: an outline, a template, or rules.','Cho AI biết cách viết ghi chú: dàn ý, mẫu hoặc quy tắc.')}),ta,
  E('div',{className:'up'},upb,E('small',{textContent:L('Replaces the text above · max 2,000 characters','Thay cho nội dung trên · tối đa 2.000 ký tự')}),ok),file);
 const opts=async n=>{const b={plain:plain.checked?1:0,instr:useN.checked?1:0};if(n)b.notes=ta.value;
  try{const r=await api('/api/ai_opts',b);if(r.status===503)throw L('Put a microSD card in the Sticky first.','Hãy lắp thẻ microSD vào Sticky trước.');if(!r.ok)throw '';
   if(n){ok.textContent=L('Saved','Đã lưu');setTimeout(()=>ok.textContent='',2000)}}
  catch(e){alertBox(L('Couldn\'t save','Không lưu được'),typeof e==='string'?e:'')}};
 file.onchange=async()=>{const f=file.files[0];if(!f)return;let t=await f.text();file.value='';
  if([...t].length>2000){t=[...t].slice(0,2000).join('');alertBox(L('The file is longer than 2,000 characters','Tệp dài hơn 2.000 ký tự'),L('Only the first 2,000 were kept.','Chỉ giữ 2.000 ký tự đầu.'))}ta.value=t;opts(1)};
 const sync=()=>{useN.disabled=plain.checked;nbox.hidden=plain.checked||!useN.checked};
 plain.onchange=useN.onchange=()=>{sync();opts()};ta.onchange=()=>opts(1);sync();
 if(!A.sd){ta.disabled=upb.disabled=true;ta.placeholder=L('Put a microSD card in the Sticky first.','Hãy lắp thẻ microSD vào Sticky trước.')}
 v.append(E('div',{className:'tg'},E('div',{},E('label',{htmlFor:'a-plain',textContent:L('Plain text','Văn bản gốc')}),
   E('p',{className:'sd',textContent:L('Save your exact words as the note. The AI only picks to-do or note.','Lưu đúng lời bạn nói thành ghi chú. AI chỉ chọn việc cần làm hay ghi chú.')})),plain),
  E('div',{className:'tg'},E('label',{htmlFor:'a-instr',textContent:L('Note instructions','Hướng dẫn viết ghi chú')}),useN),nbox)}
// ---------- To-do (06o / 06p) and Notes (06q / 06r) ----------
async function counts(){try{C=await(await api('/api/counts')).json()}catch(e){}render()}
let TD={tasks:[],now:''},N=null,ST=0;
const p2=n=>String(n).padStart(2,'0'),dmy=c=>c?c.slice(8,10)+'/'+c.slice(5,7)+'/'+c.slice(2,4):'';
const ico=b=>H('<svg class="ico" viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">'+b+'</svg>');
const MON=['January','February','March','April','May','June','July','August','September','October','November','December'],MS=MON.map(m=>m.slice(0,3));
const DAYS=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'],DV=['CN','T2','T3','T4','T5','T6','T7'];
const fmtDate=s=>{const[y,m,d]=s.split('-').map(Number),w=new Date(Date.UTC(y,m-1,d)).getUTCDay();return L(DAYS[w]+' '+d+' '+MS[m-1]+' '+y,DV[w]+' '+p2(d)+'/'+p2(m)+'/'+y)};
const fmtTime=t=>{const h=Math.floor(t/60);return((h+11)%12+1)+':'+p2(t%60)+' '+(h>=12?'PM':'AM')};
const stamp=c=>dmy(c)+(c&&c.length==16?' '+fmtTime(+c.slice(11,13)*60+ +c.slice(14)):'');  // "20/11/26 9:05 AM"
const VOICE=()=>L('Add one here, or hold ● on the Sticky to add one by voice.','Thêm ở đây, hoặc giữ ● trên Sticky để thêm bằng giọng nói.');
const empty=(t,h)=>E('div',{className:'emp'},E('b',{textContent:t}),E('p',{textContent:h}));
const sw=on=>E('input',{type:'checkbox',className:'sw',checked:on});
async function getJ(p){try{const r=await api(p);if(r.status===503)throw 503;if(!r.ok)throw 0;return await r.json()}
 catch(e){alertBox(e===503?L('Put a microSD card in the Sticky first.','Hãy lắp thẻ microSD vào Sticky trước.'):L('Couldn\'t reach the Sticky','Không kết nối được Sticky'));return null}}
async function post(p,b){let r;try{r=await api(p,b)}catch(e){r={status:0}}ST=r.status;if(r.ok)return r;
 alertBox(ST===503?L('Put a microSD card in the Sticky first.','Hãy lắp thẻ microSD vào Sticky trước.'):ST===409||ST===404?L('Changed on the Sticky','Đã thay đổi trên Sticky'):ST===413?L('The note is too long','Ghi chú quá dài'):ST?L('Couldn\'t save','Không lưu được'):L('Couldn\'t reach the Sticky','Không kết nối được Sticky'),
  ST===409||ST===404?L('Go back to see the list as it is now.','Quay lại để xem danh sách hiện tại.'):'');return null}

async function todoView(){const v=$('v-todo'),d=await getJ('/api/todo');if(!d)return;TD=d;
 const add=E('button',{className:'btn k',style:'margin:0 0 22px',textContent:L('+  Add task','+  Thêm việc')});add.onclick=()=>nav('task','new');
 if(!d.tasks.length){v.replaceChildren(add,empty(L('No tasks yet','Chưa có việc nào'),VOICE()));return}
 const rows=d.tasks.map(t=>{const cb=E('button',{className:'ckb'+(t.done?' on':''),ariaLabel:L('Done','Xong')});if(t.done)cb.append(check());
  cb.onclick=async e=>{e.stopPropagation();await post('/api/todo_set',{i:t.i,was:t.was,text:t.text,due:t.due,done:t.done?0:1});todoView()};
  const tx=E('span',{className:'t'},E('b',{className:t.done?'dn':'',textContent:t.text}));if(t.label)tx.append(E('small',{textContent:t.label}));
  const r=E('div',{className:'r',role:'button',tabIndex:0},cb,tx,E('span',{className:'ch',textContent:'›'}));r.onclick=()=>nav('task',String(t.i));return r});
 v.replaceChildren(add,E('div',{className:'g'},...rows))}

// 06p: Date and Time each have a switch; on, a month calendar / an hour : min : AM-PM wheel opens (tap the name to show / hide it).
// Same deadline rules as the Sticky: Date only = a date-only deadline, Time only = today at that time.
function taskView(){const v=$('v-task'),nw=det==='new',src=nw?null:TD.tasks.find(t=>String(t.i)===det);
 if(!nw&&!src){v.replaceChildren();return}
 const today=TD.now?TD.now.slice(0,10):new Date().toISOString().slice(0,10),due=src?src.due:'';
 const T={text:src?src.text:'',date:due.slice(0,10),time:due.length===16?+due.slice(11,13)*60+ +due.slice(14):-1,dOpen:!nw&&!!due&&due.length<16,tOpen:false};
 [T.cy,T.cm]=(T.date||today).split('-').map(Number);
 const o0=JSON.stringify([T.text,T.date,T.time]);dirty=()=>JSON.stringify([T.text.trim(),T.date,T.time])!==o0;
 const ta=E('textarea',{id:'t-text',value:T.text,placeholder:L('Task','Việc'),maxLength:300,style:'height:84px;font-size:16px'});
 ta.oninput=()=>{ta.value=ta.value.replace(/\n/g,' ');T.text=ta.value};
 const card=E('div',{className:'g',style:'margin:14px 0 0'});let tSub;
 const head=(icon,name,sub,on,flip,open)=>{const s=sw(on),t=E('span',{className:'t'},E('b',{textContent:name}));if(on)t.append(sub);
  s.onchange=()=>flip(s.checked);t.onclick=()=>{if(on)open()};return E('div',{className:'dt'},ico(icon),t,s)};
 const calendar=()=>{const y=T.cy,m=T.cm,lead=(new Date(Date.UTC(y,m-1,1)).getUTCDay()+6)%7,n=new Date(Date.UTC(y,m,0)).getUTCDate();
  const pv=E('button',{type:'button',textContent:'‹'}),nx=E('button',{type:'button',textContent:'›'});
  pv.onclick=()=>{if(--T.cm<1){T.cm=12;T.cy--}draw()};nx.onclick=()=>{if(++T.cm>12){T.cm=1;T.cy++}draw()};
  const g=E('div',{className:'gd'},...L(['Mo','Tu','We','Th','Fr','Sa','Su'],['T2','T3','T4','T5','T6','T7','CN']).map(w=>E('span',{className:'wd',textContent:w})));
  for(let i=0;i<lead;i++)g.append(E('span'));
  for(let d=1;d<=n;d++){const s=y+'-'+p2(m)+'-'+p2(d),b=E('button',{type:'button',textContent:d,className:(s===T.date?'sel':'')+(s===today?' td':'')});b.onclick=()=>{T.date=s;draw()};g.append(b)}
  return E('div',{className:'cal'},E('div',{className:'mh'},E('b',{textContent:L(MON[m-1]+' '+y,'Tháng '+m+' '+y)}),pv,nx),g)};
 const wheel=()=>{const h=Math.floor(T.time/60);let hi=h%12,mm=T.time%60,ap=h>=12?1:0;
  const upd=()=>{T.time=(hi+ap*12)*60+mm;tSub.textContent=fmtTime(T.time)};
  const col=(vals,idx,on)=>{const c=E('div',{className:'col'},E('i'),...vals.map((t,k)=>{const d=E('div',{textContent:t});d.onclick=()=>c.scrollTo({top:k*36,behavior:'smooth'});return d}),E('i'));
   const mark=k=>[...c.children].forEach((d,j)=>{d.style.color=j-1===k?'#000':'#999';d.style.fontWeight=j-1===k?'700':''});mark(idx);
   c.dataset.i=idx;let tm;c.onscroll=()=>{clearTimeout(tm);tm=setTimeout(()=>{const k=Math.round(c.scrollTop/36);mark(k);on(k);upd()},120)};return c};
  return E('div',{className:'wh'},E('div',{className:'band'}),col(['12','1','2','3','4','5','6','7','8','9','10','11'],hi,k=>hi=k),E('span',{className:'co',textContent:':'}),
   col([...Array(60)].map((_,k)=>p2(k)),mm,k=>mm=k),col(['AM','PM'],ap,k=>ap=k))};
 function draw(){tSub=E('small',{textContent:T.time>=0?fmtTime(T.time):''});
  const dr=head('<rect x="3" y="4" width="18" height="18" rx="2"/><path d="M16 2v4M8 2v4M3 10h18"/>',L('Date','Ngày'),E('small',{textContent:T.date?fmtDate(T.date):''}),!!T.date,
   on=>{T.date=on?T.date||today:'';T.dOpen=on;if(on)[T.cy,T.cm]=T.date.split('-').map(Number);draw()},()=>{T.dOpen=!T.dOpen;draw()});
  const tr=head('<circle cx="12" cy="12" r="10"/><path d="M12 6v6l4 2"/>',L('Time','Giờ'),tSub,T.time>=0,
   on=>{T.time=on?(T.time>=0?T.time:9*60):-1;T.tOpen=on;draw()},()=>{T.tOpen=!T.tOpen;draw()});
  card.replaceChildren(dr,...(T.date&&T.dOpen?[calendar()]:[]),tr,...(T.time>=0&&T.tOpen?[wheel()]:[]));
  setTimeout(()=>{for(const c of card.querySelectorAll('.col'))c.scrollTop=c.dataset.i*36},0)}
 draw();
 const save=E('button',{type:'button',className:'k',textContent:L('Save','Lưu')});
 save.onclick=async()=>{const text=T.text.trim();if(!text)return alertBox(L('Type the task first','Hãy nhập việc trước'));
  const due=T.date||T.time>=0?(T.date||today)+(T.time>=0?' '+p2(Math.floor(T.time/60))+':'+p2(T.time%60):''):'';
  if(await post('/api/todo_set',{i:nw?-1:src.i,was:nw?'':src.was,text,due,done:src&&src.done?1:0})){dirty=()=>false;history.back()}else if(ST===409)dirty=()=>false};
 const btns=E('div',{className:'two'});
 if(!nw){const del=E('button',{type:'button',textContent:L('Delete','Xoá')});btns.append(del);
  del.onclick=()=>alertBox(L('Delete task?','Xoá việc này?'),L('This can’t be undone.','Không thể hoàn tác.'),L('Delete','Xoá'),async()=>{if(await post('/api/todo_del',{i:src.i,was:src.was})){dirty=()=>false;history.back()}})}
 btns.append(save);
 v.replaceChildren(E('label',{htmlFor:'t-text',textContent:L('Task','Việc')}),ta,card,...(src&&src.created?[E('p',{className:'cr',textContent:L('Created ','Ngày tạo ')+stamp(src.created)})]:[]),btns)}

async function notesView(){const v=$('v-notes'),d=await getJ('/api/notes');if(!d)return;
 const add=E('button',{className:'btn k',style:'margin:0 0 22px',textContent:L('+  Add note','+  Thêm ghi chú')});add.onclick=()=>nav('noteed','new');
 if(!d.notes.length){v.replaceChildren(add,empty(L('No notes yet','Chưa có ghi chú'),VOICE()));return}
 v.replaceChildren(add,E('div',{className:'g'},...d.notes.map(n=>{const r=E('div',{className:'r',role:'button',tabIndex:0},E('span',{className:'t'},E('b',{textContent:n.title||L('Untitled','Không tiêu đề')}),E('small',{className:'pr',textContent:n.preview})),E('span',{className:'ch',textContent:'›'}));
  r.onclick=()=>nav('note',String(n.num));return r})))}

async function noteView(){const v=$('v-note');v.replaceChildren();const n=await getJ('/api/note?num='+det);if(!n)return;N=n;
 const ed=E('button',{type:'button',className:'k',textContent:L('Edit','Sửa')}),del=E('button',{type:'button',textContent:L('Delete','Xoá')});ed.onclick=()=>nav('noteed',det);
 del.onclick=()=>alertBox(L('Delete note?','Xoá ghi chú?'),L('This can’t be undone.','Không thể hoàn tác.'),L('Delete','Xoá'),async()=>{if(await post('/api/note_del',{num:n.num}))history.back()});
 v.replaceChildren(E('div',{className:'nv'},E('h2',{textContent:n.title||L('Untitled','Không tiêu đề')}),E('p',{className:'cd',textContent:stamp(n.created)}),E('p',{className:'bd',textContent:n.body})),E('div',{className:'two'},ed,del))}

async function noteEdView(){const v=$('v-noteed'),nw=det==='new';v.replaceChildren();
 let n={num:0,title:'',body:'',created:''};if(!nw){n=N&&String(N.num)===det?N:await getJ('/api/note?num='+det);if(!n)return}
 const ti=E('input',{id:'n-title',value:n.title,placeholder:L('Title','Tiêu đề'),maxLength:120}),bo=E('textarea',{id:'n-body',value:n.body,placeholder:L('Start typing…','Bắt đầu gõ…'),maxLength:3900,style:'height:260px;font-size:16px'});
 const o0=n.title+'\0'+n.body;dirty=()=>ti.value+'\0'+bo.value!==o0;
 const save=E('button',{type:'button',className:'k',textContent:L('Save','Lưu')}),btns=E('div',{className:'two'});
 save.onclick=async()=>{if(!ti.value.trim()&&!bo.value.trim())return alertBox(L('Type a title or some text first','Hãy nhập tiêu đề hoặc nội dung trước'));
  if(await post('/api/note_set',{num:n.num,title:ti.value,body:bo.value})){dirty=()=>false;N=null;history.back()}};
 if(!nw){const del=E('button',{type:'button',textContent:L('Delete','Xoá')});btns.append(del);
  del.onclick=()=>alertBox(L('Delete note?','Xoá ghi chú?'),L('This can’t be undone.','Không thể hoàn tác.'),L('Delete','Xoá'),async()=>{if(await post('/api/note_del',{num:n.num})){dirty=()=>false;view='note';history.go(-2)}})}
 btns.append(save);
 v.replaceChildren(E('label',{htmlFor:'n-title',textContent:L('Title','Tiêu đề')}),ti,E('label',{htmlFor:'n-body',textContent:L('Note','Ghi chú')}),bo,...(n.created?[E('p',{className:'cr',textContent:L('Created ','Ngày tạo ')+stamp(n.created)})]:[]),btns)}
load();counts();setInterval(load,1500);
</script></body></html>)HTML";

// The page and its API answer only on the address 04j chose: the hotspot's, or (Home Wi-Fi) the home network's.
inline IPAddress web_ip() { return g_webHome ? WiFi.localIP() : WiFi.softAPIP(); }
inline bool web_local() { return g_webOn && g_web.client().localIP() == web_ip(); }
inline bool web_ok() {
  if (web_local()) return true;
  g_web.send(404, "text/plain", "Not found");
  return false;
}
inline void font_send(const uint8_t *d, size_t n) {  // the page's web font; same bytes until the firmware changes
  if (!web_ok()) return;
  g_web.sendHeader("Cache-Control", "max-age=86400");
  g_web.send_P(200, "font/woff", (PGM_P)d, n);
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
  if (!web_ok()) return;
  g_webSeen = millis();
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
  d["wifi_on"] = g_wifiOn;
  d["home"] = g_webHome;  // 04j Home Wi-Fi: the page shows its warning
  d["lang"] = g_vi ? "vi" : "en";
  d["font"] = cfg_get("font");
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
  if (!web_ok()) return;
  String ssid = g_web.arg("ssid"), pass = g_web.arg("pass");
  if (ssid.isEmpty() || !text_ok(ssid, 32) || !text_ok(pass, 63)) return api_reply(400, "Invalid input");
  if (g_ns == NS_SCAN) return api_reply(409, "Busy");
  if (!g_wifiOn) return api_reply(409, "Wi-Fi is off");
  const int k = wl_find(g_wifi, ssid);
  if (pass.isEmpty() && k >= 0) pass = g_wifi.n[k].pass;
  g_jrSsid = ssid;
  g_jr = JR_BUSY;
  api_reply(202, "Joining");  // answer first: joining can move the hotspot to another channel
  start_join(WifiNet{ssid, pass}, true);
}

inline void api_forget() {
  if (!web_ok()) return;
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
  if (!web_ok()) return;
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
  if (!web_ok()) return;
  if (g_ns != NS_IDLE) return api_reply(409, "Busy");
  WiFi.scanNetworks(true);
  g_scanOnly = true;
  g_ns = NS_SCAN;
  g_nsAt = millis();
  api_reply(202, "Scanning");
}

// ---------- user fonts (06m, 06n): the phone converts, the Sticky only stores and checks ----------
inline void api_fonts() {
  if (!web_ok()) return;
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
    g_upOk = web_local() && g_sdOk && font_name_ok(name) && font_slot_ok(slot);
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
  if (!web_ok()) return;
  if (g_upOk && cfg_get("font") == g_web.arg("name") && g_web.arg("slot") == FONT_SLOTS[FONT_SLOTS_N - 1].file) {
    font_apply(g_web.arg("name"));  // the font in use was replaced: take the new files after the last slot
    g_pageDirty = true;
  }
  api_reply(g_upOk ? 200 : 400, g_upOk ? "OK" : "Bad font file");
  g_upOk = false;
}

inline void api_font_del() {
  if (!web_ok()) return;
  api_reply(font_delete(g_web.arg("name")) ? 200 : 400, "");
}

// City suggestions through the Sticky's own Wi-Fi (the phone on the hotspot has no internet).
inline void api_city() {
  if (!web_ok()) return;
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

// ---------- AI (06h): speech-to-text + AI for notes & to-dos, both OpenAI-compatible (PLAN_AI.md) ----------
// Defaults: one OpenRouter key for both. An empty AI URL / key means "same as speech-to-text".
constexpr const char *AI_URL_DEF = "https://openrouter.ai/api/v1";
constexpr const char *STT_MODEL_DEF = "mistralai/voxtral-small-transcribe-3-0";
constexpr const char *AI_MODEL_DEF = "deepseek/deepseek-v4.1-flash-1017";  // has zero-retention hosts on OpenRouter
constexpr const char *NOTE_INSTR_PATH = "/ai/note-instructions.txt";
constexpr size_t NOTE_INSTR_MAX = 2000;  // characters

// Keys never go back to the browser: only their first few characters, to show which one is saved.
inline String key_hint(const char *k) {
  const String s = cfg_get(k);
  return s.length() ? s.substring(0, min<unsigned>(6, s.length() / 3)) + "••••" : "";
}
inline size_t utf8_len(const String &s) {
  size_t n = 0;
  for (const char c : s) n += (uint8_t(c) & 0xC0) != 0x80;
  return n;
}
inline bool url_ok(const String &u) { return u.isEmpty() || ((u.startsWith("https://") || u.startsWith("http://")) && text_ok(u, 200) && u.indexOf(' ') < 0); }
inline bool hosts_ok(const String &s) {  // 06h Provider: host names, e.g. "groq, deepinfra"
  if (s.length() > 100) return false;
  for (const char c : s)
    if (!isalnum(uint8_t(c)) && !strchr("-_./, ", c)) return false;
  return true;
}
inline bool notes_ok(const String &s) {  // multi-line text: line breaks and tabs allowed, other control characters not
  if (utf8_len(s) > NOTE_INSTR_MAX) return false;
  for (const char c : s)
    if ((uint8_t(c) < 0x20 && c != '\n' && c != '\r' && c != '\t') || c == 0x7f) return false;
  return true;
}

inline void api_ai_get() {
  if (!web_ok()) return;
  JsonDocument d;
  d["stt_url"] = cfg_get("stt_url", AI_URL_DEF);
  d["stt_model"] = cfg_get("stt_model", STT_MODEL_DEF);
  d["stt_key"] = key_hint("stt_key");
  d["ai_url"] = cfg_get("ai_url");
  d["ai_model"] = cfg_get("ai_model", AI_MODEL_DEF);
  d["ai_key"] = key_hint("ai_key");
  d["ai_def"] = AI_MODEL_DEF;  // 06h2 Model left empty
  d["stt_order"] = cfg_get("stt_order");
  d["ai_order"] = cfg_get("ai_order");
  d["ai_sort"] = cfg_get("ai_sort");
  d["stt_set"] = cfg_get("stt_key").length() > 0;  // a passed test saves the key; 06h4 deletes it
  d["ai_set"] = cfg_get("ai_model").length() > 0;  // the AI may borrow the key, so its model marks it saved
  d["plain"] = cfg_get("ai_plain") == "1";
  d["instr"] = cfg_get("ai_instr", "1") == "1";
  d["sd"] = g_sdOk;
  if (g_sdOk) {
    File f = SD.open(NOTE_INSTR_PATH);
    if (f) { d["notes"] = read_upto(f, NOTE_INSTR_MAX * 4); f.close(); }
  }
  send_json(d);
}

// 06h2 toggles save as they flip; note instructions (only when sent) go to the card, written as .tmp then renamed.
inline void api_ai_opts() {
  if (!web_ok()) return;
  if (g_web.hasArg("notes")) {
    const String notes = g_web.arg("notes");
    if (!notes_ok(notes)) return api_reply(400, "Invalid input");
    File cur = g_sdOk ? SD.open(NOTE_INSTR_PATH) : File();
    const bool same = cur ? read_upto(cur, NOTE_INSTR_MAX * 4) == notes : notes.isEmpty();
    if (cur) cur.close();
    if (!same) {
      if (!g_sdOk) return api_reply(503, "No SD card");
      if (!SD.exists("/ai")) SD.mkdir("/ai");
      SD.remove(NOTE_INSTR_PATH);
      if (notes.length()) {
        const char *tmp = "/ai/note-instructions.tmp";
        File f = SD.open(tmp, FILE_WRITE);
        const bool ok = f && f.print(notes) == notes.length();
        if (f) f.close();
        if (!ok || !SD.rename(tmp, NOTE_INSTR_PATH)) return api_reply(500, "SD write failed");
      }
    }
  }
  cfg_set("ai_plain", g_web.arg("plain") == "1" ? "1" : "0");
  cfg_set("ai_instr", g_web.arg("instr") == "0" ? "0" : "1");
  api_reply(200, "OK");
}

// 06h4: forget one service's URL, model, key and provider choice.
inline void api_ai_del() {
  if (!web_ok()) return;
  const String p = g_web.arg("svc") == "stt" ? "stt_" : "ai_";
  for (const char *k : {"url", "model", "key", "order", "auto", "sort"}) cfg_del((p + k).c_str());  // "auto": the old 06h2 switch
  api_reply(200, "OK");
}

inline void wifi_set_on(bool on);
inline void api_wifi_on() {  // 06b switch, saved as it flips
  if (!web_ok()) return;
  const String v = g_web.arg("on");
  if (v != "0" && v != "1") return api_reply(400, "Invalid input");
  wifi_set_on(v == "1");
  api_reply(200, "OK");
}

inline void api_ai_test();  // 06h1/06h2 Save, in ai.h (it needs ai_post)

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
  g_wifiOn = cfg_get("wifi_on", "1") != "0";
  const String off = cfg_get("utc_off");  // last known offset: right clock before the first weather reply
  if (off.length()) g_utc_off = off.toInt();
  configTime(off.toInt(), 0, "pool.ntp.org", "time.google.com");
  g_bootClock = time(nullptr) - millis() / 1000;
  sntp_set_time_sync_notification_cb([](timeval *tv) { g_ntpOk = true; g_netTime = tv->tv_sec; });
  g_web.on("/", HTTP_GET, [] {
    if (!web_ok()) return;
    g_web.sendHeader("Cache-Control", "no-store");  // a new firmware's page shows on the next reload
    g_web.send_P(200, "text/html; charset=utf-8", PAGE);
  });
  g_web.on("/font/r.woff", HTTP_GET, [] { font_send(WEBFONT_R, sizeof WEBFONT_R); });
  g_web.on("/font/b.woff", HTTP_GET, [] { font_send(WEBFONT_B, sizeof WEBFONT_B); });
  g_web.on("/api/status", HTTP_GET, api_status);
  g_web.on("/api/city", HTTP_GET, api_city);
  g_web.on("/api/join", HTTP_POST, api_join);
  g_web.on("/api/forget", HTTP_POST, api_forget);
  g_web.on("/api/save", HTTP_POST, api_save);
  g_web.on("/api/scan", HTTP_POST, api_scan);
  g_web.on("/api/wifi_on", HTTP_POST, api_wifi_on);
  g_web.on("/api/fonts", HTTP_GET, api_fonts);
  g_web.on("/api/font", HTTP_POST, api_font_done, api_font_part);
  g_web.on("/api/font_del", HTTP_POST, api_font_del);
  g_web.on("/api/ai", HTTP_GET, api_ai_get);
  g_web.on("/api/ai_opts", HTTP_POST, api_ai_opts);
  g_web.on("/api/ai_del", HTTP_POST, api_ai_del);
  g_web.on("/api/ai_test", HTTP_POST, api_ai_test);
  g_web.onNotFound([] {  // captive portal: any other address -> the page
    if (!web_ok()) return;
    g_web.sendHeader("Location", "http://" + web_ip().toString() + "/");
    g_web.send(302, "text/plain", "");
  });
  Serial.printf("[net] %d saved network(s)\n", g_wifi.count);
  if (!g_wifi.count) net_web(true);
  return g_wifi.count > 0;
}

// ---------- HTTPS JSON (certificates checked against the core's built-in root CA bundle) ----------
inline bool http_json(const String &url, JsonDocument &doc) {
  NetworkClientSecure tls;
  tls.useBuiltinCACertBundle();
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(tls, url)) return false;
  const char *keys[] = {"Date"};
  http.collectHeaders(keys, 1);
  const int code = http.GET();
  if (code > 0 && !g_ntpOk) {  // networks that block NTP (public Wi-Fi) still pass HTTPS: take the time from the reply
    const long long t = http_date(http.header("Date").c_str());
    if (t > 1700000000LL) { timeval tv = {time_t(t), 0}; settimeofday(&tv, nullptr); g_netTime = t; }
  }
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
constexpr uint32_t WX_EVERY_MS = 60UL * 60 * 1000, WX_RETRY_MS = 60UL * 1000;

inline bool net_weather(Weather &w) {
  if (WiFi.status() != WL_CONNECTED || !net_geocode()) return false;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + cfg_get("lat") + "&longitude=" + cfg_get("lon") +
               "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1";
  if (cfg_get("units", "C") == "F") url += "&temperature_unit=fahrenheit";
  JsonDocument d;
  if (!http_json(url, d)) return false;
  w = {true, d["current"]["temperature_2m"].as<float>(), d["daily"]["temperature_2m_max"][0].as<float>(),
          d["daily"]["temperature_2m_min"][0].as<float>(), d["current"]["weather_code"].as<int>(), millis()};
  const long off = d["utc_offset_seconds"].as<long>();
  if (off != g_utc_off) {  // follows DST within the hour; kept for the next boot
    g_utc_off = off;
    cfg_set("utc_off", String(off));
    configTime(off, 0, "pool.ntp.org", "time.google.com");
  }
  Serial.printf("[wx] %.1f (%.0f/%.0f) code=%d %s  utc%+ld\n", g_wx.temp, g_wx.hi, g_wx.lo, g_wx.code, wx_info(w.code).label, off);
  return true;
}

// The weather calls (two HTTPS requests, up to 8 s each) run in their own task on core 0, like the AI calls,
// so touches, buttons and the phone page keep working. net_loop() takes the result.
enum WxJob : uint8_t { WXJ_IDLE, WXJ_RUNNING, WXJ_OK, WXJ_FAIL };
inline volatile WxJob g_wxJob = WXJ_IDLE;
inline Weather g_wxNew;

inline void wx_task(void *) {
  Weather w;
  const bool ok = net_weather(w);
  if (ok) g_wxNew = w;
  g_wxJob = ok ? WXJ_OK : WXJ_FAIL;
  vTaskDelete(nullptr);
}

// Off: leave the network now (the hotspot, if on, stays up) and show weather as Offline. On: join the strongest saved one.
inline void wifi_set_on(bool on) {
  if (on == g_wifiOn) return;
  g_wifiOn = on;
  cfg_set("wifi_on", on ? "1" : "0");
  g_jr = JR_NONE;
  if (on) { g_retryAt = 0; if (g_ns == NS_IDLE) join_best_cached(); }
  else {
    WiFi.disconnect();
    if (g_ns == NS_JOIN) g_ns = NS_IDLE;  // a running scan finishes on its own and joins nothing
    g_wasOn = false;
    g_wx.ok = false;
  }
  g_pageDirty = true;  // status bar Wi-Fi icon and weather card now, not at the next minute
  Serial.printf("[net] Wi-Fi %s\n", on ? "on" : "off");
}

// The RTC keeps network time through power-off and corrects light sleep's drift. Main loop only (Wire1).
inline void net_store_time() {
  if (!g_netTime) return;
  const time_t t = g_netTime;
  g_netTime = 0;
  Serial.printf("[rtc] %s\n", hw_rtc_write(t) ? "stored network time" : "write failed");
}

inline void net_loop() {
  // Once per turn-on: the RTC stops while the Sticky is off, so ask NTP as soon as Wi-Fi is up (SNTP started
  // before Wi-Fi at boot, its first request failed and it is waiting on its own backoff).
  static bool asked = false, logged = false;
  if (!asked && WiFi.status() == WL_CONNECTED) {
    asked = true;
    configTime(g_utc_off == LONG_MIN ? 0 : g_utc_off, 0, "pool.ntp.org", "time.google.com");  // restarts SNTP: asks now
    Serial.println("[ntp] turn-on time check");
  }
  if (!logged && g_netTime) {
    logged = true;
    Serial.printf("[ntp] turn-on check: clock was %lld s off\n", (long long)(g_netTime - g_bootClock - millis() / 1000));
  }
  net_store_time();
  net_sta_step();
  if (g_webOn) { g_dns.processNextRequest(); g_web.handleClient(); }
  if (g_wxJob == WXJ_OK || g_wxJob == WXJ_FAIL) {
    const bool ok = g_wxJob == WXJ_OK;
    if (ok && g_wifiOn) g_wx = g_wxNew;  // Wi-Fi switched off meanwhile: stays Offline
    if (g_wx_next) g_wx_next = millis() + (ok ? WX_EVERY_MS : WX_RETRY_MS);  // 0: asked again meanwhile (new city, units)
    g_wxJob = WXJ_IDLE;
  }
  if (g_wxJob == WXJ_IDLE && WiFi.status() == WL_CONNECTED && millis() >= g_wx_next) {
    g_wxJob = WXJ_RUNNING;
    g_wx_next = UINT32_MAX;
    if (xTaskCreatePinnedToCore(wx_task, "wx", 12288, nullptr, 1, nullptr, 0) != pdPASS) { g_wxJob = WXJ_IDLE; g_wx_next = millis() + WX_RETRY_MS; }
  }
  if (g_wx.ok && millis() - g_wx.at > WX_EVERY_MS + 2 * WX_RETRY_MS) g_wx.ok = false;  // stale -> Offline
}
