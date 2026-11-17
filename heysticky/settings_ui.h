// Settings screens, Figma 04-04k: Settings, Configuration, Temperature, About, SD Card, SD Files, Web setup. Drawing and hit areas only; screens.h navigates.
#pragma once
#include <qrcode.h>  // esp_qrcode, bundled with the ESP32 core
#include <esp_efuse.h>
#include "ui.h"
#include "card.h"
#include "net.h"
#include "settings.h"

// ---------- shared rows ----------
constexpr int SROW_TOP = 112, SROW_H = 72, IROW_H = 66;  // settings rows (Bold 28) / info rows (Bold 25)

// Label left, value right (cut to fit), chevron when the row opens a screen, 1 px divider under it.
inline void info_row(int y, int h, const Font &lf, const String &label, const String &value, bool chev) {
  const int lh = &lf == &F_B28 ? 37 : 33;
  const int lw = ui_text(lf, label, 40, y + (h - lh) / 2, lh);
  const int right = chev ? 406 : 440;
  if (chev) ui_icon(ICON_CHEV, 414, y + (h - 26) / 2);
  if (value.length()) ui_text(F_R25, ui_fit(F_R25, value, right - 40 - lw - 12), right, y + (h - 33) / 2, 33, AL_RIGHT);
  display.fillRect(40, y + h - 1, 400, 1, INK);
}

inline int srow_at(int y, int h, int n) {  // row index under y, or -1
  return y >= SROW_TOP && y < SROW_TOP + h * n ? (y - SROW_TOP) / h : -1;
}

// ---------- 04 Settings ----------
enum SetRow : uint8_t { SR_ROTATE, SR_SOUND, SR_LANG, SR_UPDATE, SR_CONFIG, SR_WEB, SR_ABOUT, SR_COUNT };
inline const struct { const Icon *icon; const char *label; } SET_ROWS[SR_COUNT] = {
  {&ICON_SET_ROTATE, "Auto-rotate"}, {&ICON_SET_SOUND, "Sound"}, {&ICON_SET_LANG, "Language & Font"},
  {&ICON_SET_UPDATE, "Software Update"}, {&ICON_SET_CONFIG, "Configuration"}, {&ICON_SET_WEB, "Web setup"},
  {&ICON_SET_ABOUT, "About"},
};

inline bool set_rotate() { return cfg_get("rotate") == "1"; }  // stored only: no screen rotates yet (needs the IMU)

inline void draw_settings() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar("Settings", "");
  for (int i = 0; i < SR_COUNT; i++) {
    const int y = SROW_TOP + i * SROW_H;
    ui_icon(*SET_ROWS[i].icon, 40, y + 20);
    ui_text(F_B28, SET_ROWS[i].label, 86, y + 17, 37);
    if (i == SR_ROTATE || i == SR_SOUND) ui_toggle(372, y + 17, i == SR_ROTATE ? set_rotate() : g_sound);
    else ui_icon(ICON_CHEV, 414, y + 23);
    display.fillRect(40, y + SROW_H - 1, 400, 1, INK);
  }
}

// ---------- 04e Configuration (City is display only: set on the phone setup page) ----------
inline String units() { return cfg_get("units", "C"); }

inline void draw_config() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar("Configuration", "");
  const String city = cfg_get("city");
  info_row(SROW_TOP, SROW_H, F_B28, "City", city.length() ? city : "—", false);
  info_row(SROW_TOP + SROW_H, SROW_H, F_B28, "Timezone", utc_text(g_utc_off), false);
  info_row(SROW_TOP + 2 * SROW_H, SROW_H, F_B28, "Refresh interval", "30 min", false);
  info_row(SROW_TOP + 3 * SROW_H, SROW_H, F_B28, "Temperature", units() == "F" ? "°F" : "°C", true);
}

// ---------- 04g Temperature ----------
inline void checkbox(int x, int y, bool on) {  // Figma checkbox 36x36, 3 px, radius 4
  if (on) { display.fillRoundRect(x, y, 36, 36, 4, INK); ui_icon(ICON_CHECK26, x + 5, y + 5, PAPER); }
  else ui_card(x, y, 36, 36, 4, 3);
}

inline void draw_temp() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar("Temperature", "");
  const bool f = units() == "F";
  for (int i = 0; i < 2; i++) {
    const int y = SROW_TOP + i * SROW_H;
    ui_text(F_B28, i ? "Fahrenheit (°F)" : "Celsius (°C)", 40, y + 17, 37);
    checkbox(404, y + 18, i == f);
    display.fillRect(40, y + SROW_H - 1, 400, 1, INK);
  }
}

// ---------- 04h About ----------
inline String about_sn() {
  uint8_t b[8] = {};
  esp_efuse_read_block(EFUSE_BLK3, b, 0, 64);  // user-data block, written at the factory
  return sn_text(b);
}

inline String sd_free_text() {
  if (!g_sdOk) return sd_inserted() ? "Not mounted" : "No card";
  return size_text(SD.totalBytes() - SD.usedBytes()) + " free";
}

constexpr int ABOUT_SD_ROW = 5;
inline void draw_about() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar("About", "");
  // ponytail: certifications are a static line until the device label's text is confirmed (PLAN §12)
  const String rows[][2] = {{"Name", "HeySticky"}, {"SN", about_sn()}, {"MAC", WiFi.macAddress()},
                            {"Software version", FW_VERSION}, {"Certifications", "FCC · CE"}, {"SD card", sd_free_text()}};
  for (int i = 0; i < 6; i++) info_row(SROW_TOP + i * IROW_H, IROW_H, F_B25, rows[i][0], rows[i][1], i == ABOUT_SD_ROW);
}

// ---------- 04i SD Card ----------
constexpr int SD_FILES_ROW = 4;
inline void draw_sdcard() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar("SD Card", "");
  const String space = g_sdOk ? size_text(SD.totalBytes() - SD.usedBytes()) + " / " + size_text(SD.totalBytes()) : "—";
  info_row(SROW_TOP, IROW_H, F_B25, "Inserted", sd_inserted() ? "Yes" : "No", false);
  info_row(SROW_TOP + IROW_H, IROW_H, F_B25, "Mounted", g_sdOk ? "Yes" : "No", false);
  info_row(SROW_TOP + 2 * IROW_H, IROW_H, F_B25, "Path", "/sd", false);
  info_row(SROW_TOP + 3 * IROW_H, IROW_H, F_B25, "Space", space, false);
  const String n = g_files.size() >= SD_FILES_MAX ? String(SD_FILES_MAX) + "+" : String(g_files.size());
  info_row(SROW_TOP + 4 * IROW_H, IROW_H, F_B25, "Files", g_sdOk ? n : "—", g_sdOk);
}

// ---------- SD Files: every file on the card, by path ----------
constexpr int FILES_ROWS = 9;
inline int g_filesPage = 0;
inline int files_pages() { return max(1, (int(g_files.size()) + FILES_ROWS - 1) / FILES_ROWS); }

inline void draw_files() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar("SD Files", String(g_files.size()));
  if (g_files.empty()) { ui_text(F_R25, "No files", W / 2, 318, 33, AL_CENTER); return; }
  g_filesPage = constrain(g_filesPage, 0, files_pages() - 1);
  for (int r = 0; r < FILES_ROWS; r++) {
    const int i = g_filesPage * FILES_ROWS + r;
    if (i >= int(g_files.size())) break;
    const String size = size_text(g_files[i].size);
    const int y = SROW_TOP + r * IROW_H;
    ui_text(F_B25, ui_fit(F_B25, g_files[i].path, 400 - ui_width(F_R25, size) - 12), 40, y + 16, 33);
    ui_text(F_R25, size, 440, y + 16, 33, AL_RIGHT);
    display.fillRect(40, y + IROW_H - 1, 400, 1, INK);
  }
  if (files_pages() > 1) ui_page_counter(String(g_filesPage + 1) + "/" + files_pages());
}

// ---------- 04j Web setup ----------
static int s_qrX, s_qrY, s_qrS;
static void qr_modules(esp_qrcode_handle_t q) {
  const int n = esp_qrcode_get_size(q);
  s_qrS = 157 / n;  // whole pixels per module inside the 181 px frame
  const int x0 = 150 + (181 - n * s_qrS) / 2, y0 = 274 + (181 - n * s_qrS) / 2;
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(q, x, y)) display.fillRect(x0 + x * s_qrS, y0 + y * s_qrS, s_qrS, s_qrS, INK);
}

inline void draw_websetup() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_text(F_X32, "Web setup", 40, 54, 42);
  ui_toggle(372, 56, g_webOn);
  display.fillRect(40, 109, 400, 3, INK);
  if (!g_webOn) return;  // Off: page not served, nothing to show
  const bool ap = g_ap;
  ui_text(F_B25, ap ? "Join this Wi-Fi" : "Open this on your phone", W / 2, 130, 33, AL_CENTER);
  display.fillRoundRect(40, 175, 400, 87, 12, INK);
  ui_text(F_X42, ap ? AP_SSID : net_ip(), W / 2, 191, 55, AL_CENTER, PAPER, INK);
  ui_card(150, 274, 181, 181, 6, 2);
  const String qr = ap ? String("WIFI:T:nopass;S:") + AP_SSID + ";;" : "http://" + net_ip() + "/";
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = qr_modules;
  esp_qrcode_generate(&cfg, qr.c_str());
  if (ap) {
    ui_text(F_R25, "Scan to join, then open", W / 2, 467, 35, AL_CENTER);
    ui_text(F_B25, net_ip(), W / 2, 502, 35, AL_CENTER);
  } else {
    ui_text(F_R25, "Use the same Wi-Fi.", W / 2, 467, 35, AL_CENTER);
    ui_wrap(F_R25, "No Wi-Fi yet? Join the hotspot “HeySticky-Setup”, then open 192.168.4.1", 40, 514, 400, 35, 3, AL_CENTER);
  }
}
