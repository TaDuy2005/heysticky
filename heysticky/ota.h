// Software Update (TASK-13, PLAN §9): latest GitHub Release -> its heysticky-app.bin over HTTPS -> HTTPUpdate writes
// the spare OTA slot -> restart. Rollback: the new image must reach the end of setup() (ota_mark_good) or the
// bootloader goes back to the old one on the next reset.
#pragma once
#include <HTTPUpdate.h>
#include <esp_ota_ops.h>
#include "net.h"
#include "settings.h"

constexpr char OTA_REPO[] = "OWNER/heysticky";  // ponytail: placeholder until the public repo exists; checks fail until then
constexpr char OTA_ASSET[] = "heysticky-app.bin";
constexpr uint32_t OTA_AUTO_GAP_MS = 60UL * 60 * 1000;  // auto checks at most hourly (GitHub: 60 calls/h without a token)

enum OtaState : uint8_t { OTA_IDLE, OTA_CHECKING, OTA_LATEST, OTA_AVAIL, OTA_ERR, OTA_INSTALLING, OTA_FAILED };
inline OtaState g_ota = OTA_IDLE;
inline String g_otaTag, g_otaUrl;  // latest release and its app .bin
inline int g_otaPct = 0;

inline void ota_mark_good() {
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) Serial.printf("[ota] running v%s, marked good\n", FW_VERSION);
}

inline void ota_check() {
  if (WiFi.status() != WL_CONNECTED) { g_ota = OTA_IDLE; return; }
  JsonDocument d;
  if (!http_json(String("https://api.github.com/repos/") + OTA_REPO + "/releases/latest", d)) { g_ota = OTA_ERR; return; }
  g_otaTag = d["tag_name"].as<String>();
  g_otaUrl = "";
  for (JsonObject a : d["assets"].as<JsonArray>())
    if (a["name"] == OTA_ASSET) g_otaUrl = a["browser_download_url"].as<String>();
  const bool newer = ver_newer(g_otaTag.c_str(), FW_VERSION);
  g_ota = !newer ? OTA_LATEST : g_otaUrl.length() ? OTA_AVAIL : OTA_ERR;  // newer but no app .bin: can't install
  cfg_set("ota_last", String(long(time(nullptr))));
  Serial.printf("[ota] latest %s (running v%s): %s\n", g_otaTag.c_str(), FW_VERSION, newer ? "update" : "up to date");
}

// "Last checked today 09:30" / "Last checked 12/11 09:30"; "" if never (or before the clock was set).
inline String ota_checked_text() {
  const time_t at = cfg_get("ota_last").toInt(), now = time(nullptr);
  if (at < 1700000000) return "";
  struct tm t, n;
  localtime_r(&at, &t);
  localtime_r(&now, &n);
  char s[24];
  strftime(s, sizeof s, t.tm_yday == n.tm_yday && t.tm_year == n.tm_year ? TR("today %H:%M", "hôm nay %H:%M") : "%d/%m %H:%M", &t);
  return String(TR("Last checked ", "Kiểm tra lần cuối ")) + s;
}

// Downloads and installs g_otaUrl; restarts on success. shown() redraws the screen at each 10% step.
inline void ota_install(void (*shown)()) {
  NetworkClientSecure tls;
  tls.useBuiltinCACertBundle();
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // GitHub sends the asset from another host
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.onProgress([shown](int cur, int total) {
    const int p = total > 0 ? int(cur * 10LL / total) * 10 : 0;
    if (p != g_otaPct) { g_otaPct = p; shown(); }
  });
  Serial.printf("[ota] installing %s\n", g_otaTag.c_str());
  if (httpUpdate.update(tls, g_otaUrl) == HTTP_UPDATE_OK) {  // image checked (header + SHA-256) before the slot is switched
    Serial.println("[ota] installed, restarting");
    delay(200);
    ESP.restart();
  }
  Serial.printf("[ota] failed: %s\n", httpUpdate.getLastErrorString().c_str());
  g_ota = OTA_FAILED;
}

// Checks once each time Wi-Fi comes up (at most hourly). True when the result changed.
inline bool ota_loop() {
  static bool was = false;
  static uint32_t last = 0;
  const bool on = WiFi.status() == WL_CONNECTED, up = on && !was;
  was = on;
  if (!up || g_ota == OTA_INSTALLING || (last && millis() - last < OTA_AUTO_GAP_MS)) return false;
  last = millis() | 1;
  ota_check();
  return true;
}
