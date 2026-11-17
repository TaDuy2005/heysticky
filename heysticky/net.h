// HeySticky network: Wi-Fi connect, HeySticky-Setup hotspot fallback, phone setup page,
// city lookup, weather (Open-Meteo) and NTP time.
// Known limit (PLAN.md §9): the setup page is plain HTTP on the local network.
#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <vector>
#include "store.h"

constexpr char AP_SSID[] = "HeySticky-Setup";
constexpr uint32_t WIFI_TIMEOUT_MS = 15000;

inline WebServer g_web(80);
inline DNSServer g_dns;
inline bool g_ap = false;   // true = hotspot mode
inline bool g_webOn = false;  // phone setup page served: 04j toggle on Wi-Fi (off after boot), always in hotspot mode

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

// The phone setup page is plain HTTP on the local network, so on Wi-Fi it is served only while 04j's toggle is on.
inline void net_web(bool on) {
  if (on == g_webOn) return;
  if (on) g_web.begin();
  else g_web.stop();
  g_webOn = on;
  Serial.printf("[net] setup page %s\n", on ? "on" : "off");
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
  if (g_ap) net_web(true);
  if (!g_ap) configTime(0, 0, "pool.ntp.org", "time.google.com");  // UTC until the weather reply gives the offset
  Serial.printf("[net] %s %s  ip=%s\n", g_ap ? "hotspot" : "wifi", g_ap ? AP_SSID : ssid.c_str(), net_ip().c_str());
  return !g_ap;
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

// City -> lat/lon/timezone, only when the saved city changed. Top match; picking among
// several comes with the Location screen (TASK-10).
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

// 04f Location search: up to 5 places for a typed name. False = no network / request failed.
struct Place { String name, area, tz; float lat = 0, lon = 0; };  // area: "California, United States"
inline bool net_search(const String &q, std::vector<Place> &out) {
  out.clear();
  if (WiFi.status() != WL_CONNECTED) return false;
  JsonDocument d;
  if (!http_json("https://geocoding-api.open-meteo.com/v1/search?count=5&name=" + url_enc(q), d)) return false;
  for (JsonObject r : d["results"].as<JsonArray>()) {
    Place p;
    p.name = r["name"].as<String>();
    const String a1 = r["admin1"] | "", c = r["country"] | "";
    p.area = a1.length() && c.length() ? a1 + ", " + c : a1 + c;
    p.tz = r["timezone"] | "";
    p.lat = r["latitude"];
    p.lon = r["longitude"];
    out.push_back(p);
  }
  return true;
}

// Saves a picked place as the city (no second lookup by name).
inline void net_set_place(const Place &p) {
  cfg_set("city", p.name);
  cfg_set("lat", String(p.lat, 4));
  cfg_set("lon", String(p.lon, 4));
  cfg_set("tz", p.tz);
  cfg_set("geo_city", p.name);
}

// ---------- weather ----------
enum WxIcon : uint8_t { WX_SUN, WX_PARTLY, WX_CLOUD, WX_FOG, WX_RAIN, WX_SNOW, WX_STORM };
struct WxInfo { const char *label; WxIcon icon; };

// WMO weather code -> short label + icon type. ponytail: English only; VI comes with TASK-14.
inline WxInfo wx_info(int code) {
  if (code == 0) return {"Clear", WX_SUN};
  if (code == 1) return {"Mainly clear", WX_SUN};
  if (code == 2) return {"Partly cloudy", WX_PARTLY};
  if (code == 3) return {"Overcast", WX_CLOUD};
  if (code == 45 || code == 48) return {"Fog", WX_FOG};
  if (code >= 51 && code <= 57) return {"Drizzle", WX_RAIN};
  if (code >= 61 && code <= 67) return {"Rain", WX_RAIN};
  if (code >= 71 && code <= 77) return {"Snow", WX_SNOW};
  if (code >= 80 && code <= 82) return {"Showers", WX_RAIN};
  if (code == 85 || code == 86) return {"Snow showers", WX_SNOW};
  if (code >= 95) return {"Thunderstorm", WX_STORM};
  return {"Unknown", WX_CLOUD};
}

struct Weather { bool ok = false; float temp = 0, hi = 0, lo = 0; int code = 0; uint32_t at = 0; };
inline Weather g_wx;               // ok=false -> show "Offline"
inline uint32_t g_wx_next = 0;     // millis() of the next fetch
inline long g_utc_off = LONG_MIN;  // seconds, from Open-Meteo
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
  if (off != g_utc_off) { g_utc_off = off; configTime(off, 0, "pool.ntp.org", "time.google.com"); }  // follows DST within 30 min
  Serial.printf("[wx] %.1f (%.0f/%.0f) code=%d %s  utc%+ld\n", g_wx.temp, g_wx.hi, g_wx.lo, g_wx.code, wx_info(g_wx.code).label, off);
  return true;
}

inline void net_loop() {
  if (g_ap) g_dns.processNextRequest();
  if (g_webOn) g_web.handleClient();
  if (!g_ap && millis() >= g_wx_next) {
    const bool ok = net_weather();
    if (!ok && millis() - g_wx.at > WX_EVERY_MS) g_wx.ok = false;  // stale or never fetched -> Offline
    g_wx_next = millis() + (ok ? WX_EVERY_MS : WX_RETRY_MS);
  }
}
