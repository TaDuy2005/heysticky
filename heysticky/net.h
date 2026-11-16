// HeySticky network: Wi-Fi connect, HeySticky-Setup hotspot fallback, phone setup page.
// Known limit (PLAN.md §9): the setup page is plain HTTP on the local network.
#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "store.h"

constexpr char AP_SSID[] = "HeySticky-Setup";
constexpr uint32_t WIFI_TIMEOUT_MS = 15000;

inline WebServer g_web(80);
inline DNSServer g_dns;
inline bool g_ap = false;   // true = hotspot mode

inline String html_esc(const String &s) {
  String o;
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += c;
    }
  }
  return o;
}

inline String net_ip() { return g_ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString(); }

inline const char PAGE_HEAD[] PROGMEM = R"(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>HeySticky setup</title><style>
body{font:16px/1.4 ui-monospace,Menlo,monospace;margin:0;padding:20px;max-width:420px;color:#000;background:#fff}
h1{font-size:24px;margin:0 0 4px}h2{font-size:18px;margin:24px 0 8px;border-bottom:2px solid #000;padding-bottom:4px}
label{display:block;font-size:14px;margin:12px 0 4px}small{color:#555}
input[type=text],input[type=password]{width:100%;box-sizing:border-box;font:inherit;padding:10px;border:2px solid #000;border-radius:8px}
.u label{display:inline-block;margin-right:20px}button{width:100%;margin-top:24px;padding:14px;font:inherit;font-weight:700;color:#fff;background:#000;border:0;border-radius:8px}
</style></head><body>)";

inline void web_root() {
  const String ssid = cfg_get("wifi_ssid"), units = cfg_get("units", "C");
  const bool hasPass = cfg_get("wifi_pass").length();
  String h = FPSTR(PAGE_HEAD);
  h += "<h1>HeySticky setup</h1><small>Connected to your Sticky at " + net_ip() + "</small>";
  h += "<form method=post action=/save><h2>Wi-Fi</h2>";
  h += "<label>Network</label><input type=text name=ssid maxlength=32 required value=\"" + html_esc(ssid) + "\">";
  // Saved password is never sent back; empty field keeps it.
  h += "<label>Password</label><input type=password name=pass maxlength=63 placeholder=\"";
  h += hasPass ? "•••••••• (unchanged)" : "";
  h += "\"><h2>Profile</h2>";
  h += "<label>Your name</label><input type=text name=name maxlength=32 value=\"" + html_esc(cfg_get("name")) + "\">";
  h += "<label>City (for weather)</label><input type=text name=city maxlength=64 value=\"" + html_esc(cfg_get("city")) + "\">";
  h += "<label>Units</label><div class=u><label><input type=radio name=units value=C";
  h += units == "C" ? " checked" : "";
  h += "> °C</label><label><input type=radio name=units value=F";
  h += units == "F" ? " checked" : "";
  h += "> °F</label></div><button>Save to Sticky</button></form>";
  h += "<p><small>Keys are stored only on this Sticky. They are never part of the firmware file you flash or share.</small></p></body></html>";
  g_web.send(200, "text/html; charset=utf-8", h);
}

inline void web_save() {
  // Trust boundary: length limits match the form; units is an enum.
  String ssid = g_web.arg("ssid"), pass = g_web.arg("pass"), name = g_web.arg("name"),
         city = g_web.arg("city"), units = g_web.arg("units");
  ssid.trim(); name.trim(); city.trim();
  if (ssid.isEmpty() || ssid.length() > 32 || pass.length() > 63 || name.length() > 32 || city.length() > 64 ||
      (units != "C" && units != "F")) {
    g_web.send(400, "text/plain", "Invalid input");
    return;
  }
  cfg_set("wifi_ssid", ssid);
  if (pass.length()) cfg_set("wifi_pass", pass);
  cfg_set("name", name);
  cfg_set("city", city);
  cfg_set("units", units);
  g_web.send(200, "text/html; charset=utf-8",
             String(FPSTR(PAGE_HEAD)) + "<h1>Saved</h1><p>Your Sticky is restarting and will join <b>" + html_esc(ssid) +
                 "</b>. Its new address appears on its screen.</p></body></html>");
  delay(1000);
  ESP.restart();
}

// Returns true when joined to the saved Wi-Fi; false = hotspot mode.
inline bool net_begin() {
  const String ssid = cfg_get("wifi_ssid");
  WiFi.persistent(false);  // our own store.h holds the credentials
  WiFi.mode(WIFI_STA);
  if (ssid.length()) {
    WiFi.begin(ssid.c_str(), cfg_get("wifi_pass").c_str());
    const uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) delay(200);
  }
  g_ap = WiFi.status() != WL_CONNECTED;
  if (g_ap) {
    // ponytail: no network scan. Scanning around hotspot start crashes the Wi-Fi driver
    // on core 3.3.12 (wifi_mode_set / ap_rx_cb); users type the network name.
    WiFi.softAP(AP_SSID);  // open hotspot, only up until Wi-Fi is set
    g_dns.start(53, "*", WiFi.softAPIP());  // captive portal: every name -> us
  }
  g_web.on("/", HTTP_GET, web_root);
  g_web.on("/save", HTTP_POST, web_save);
  g_web.onNotFound([] {
    if (!g_ap) { g_web.send(404, "text/plain", "Not found"); return; }
    g_web.sendHeader("Location", "http://" + net_ip() + "/");
    g_web.send(302, "text/plain", "");
  });
  g_web.begin();
  Serial.printf("[net] %s %s  ip=%s\n", g_ap ? "hotspot" : "wifi", g_ap ? AP_SSID : ssid.c_str(), net_ip().c_str());
  return !g_ap;
}

inline void net_loop() {
  if (g_ap) g_dns.processNextRequest();
  g_web.handleClient();
}
