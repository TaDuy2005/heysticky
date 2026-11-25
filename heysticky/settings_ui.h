// Settings screens, Figma 04-04k: Settings, Configuration, Temperature, About, SD Card, SD Files, Web setup. Drawing and hit areas only; screens.h navigates.
#pragma once
#include <qrcode.h>  // esp_qrcode, bundled with the ESP32 core
#include <esp_efuse.h>
#include "ui.h"
#include "card.h"
#include "net.h"
#include "ota.h"
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
enum SetRow : uint8_t { SR_ROTATE, SR_SOUND, SR_LANG, SR_UPDATE, SR_CONFIG, SR_SLEEP, SR_WEB, SR_ABOUT, SR_COUNT };
inline const struct { const Icon *icon; const char *label, *vi; } SET_ROWS[SR_COUNT] = {
  {&ICON_SET_ROTATE, "Auto-rotate", "Tự xoay"}, {&ICON_SET_SOUND, "Sound", "Âm thanh"},
  {&ICON_SET_LANG, "Language & Font", "Ngôn ngữ & Phông"}, {&ICON_SET_UPDATE, "Software Update", "Cập nhật phần mềm"},
  {&ICON_SET_CONFIG, "Configuration", "Cấu hình"}, {&ICON_SET_SLEEP, "Sleep after", "Ngủ sau"}, {&ICON_SET_WEB, "Web setup", "Cài đặt qua web"}, {&ICON_SET_ABOUT, "About", "Thông tin"},
};

inline bool set_rotate() { return cfg_get("rotate") == "1"; }  // stored only: no screen rotates yet (needs the IMU)

inline void draw_settings() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Settings", "Cài đặt"), "");
  for (int i = 0; i < SR_COUNT; i++) {
    const int y = SROW_TOP + i * SROW_H;
    ui_icon(*SET_ROWS[i].icon, 40, y + 20);
    ui_text(F_B28, TR(SET_ROWS[i].label, SET_ROWS[i].vi), 86, y + 17, 37);
    if (i == SR_ROTATE) ui_toggle(372, y + 17, set_rotate());
    else ui_icon(ICON_CHEV, 414, y + 23);
    if (i == SR_SOUND) ui_text(F_R25, g_sound ? TR("On", "Bật") : TR("Off", "Tắt"), 406, y + 19, 33, AL_RIGHT);
    display.fillRect(40, y + SROW_H - 1, 400, 1, INK);
  }
}

// ---------- 04d Software Update (Figma has the up-to-date state; the others reuse its layout) ----------
constexpr int UPD_BTN_Y = 333;
inline bool update_has_button() { return g_ota != OTA_CHECKING && g_ota != OTA_INSTALLING; }

inline void draw_update() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Software Update", "Cập nhật phần mềm"), "");
  ui_text(F_X32, String("HeySticky v") + FW_VERSION, 40, 144, 42);
  const String v = g_otaTag.startsWith("v") ? g_otaTag : "v" + g_otaTag, last = ota_checked_text();
  String head, sub = last.length() ? last + TR(" · over Wi-Fi", " · qua Wi-Fi") : "", btn = TR("Check for updates", "Kiểm tra cập nhật");
  switch (g_ota) {
    case OTA_IDLE: head = WiFi.status() == WL_CONNECTED ? TR("Not checked yet", "Chưa kiểm tra") : TR("No Wi-Fi connection", "Không có Wi-Fi"); break;
    case OTA_CHECKING: head = TR("Checking…", "Đang kiểm tra…"); sub = ""; break;
    case OTA_LATEST: head = TR("You’re up to date", "Đã là bản mới nhất"); break;
    case OTA_AVAIL: head = TR(v + " is available", "Có bản " + v); btn = TR("Install ", "Cài ") + v; break;
    case OTA_ERR: head = TR("Couldn’t check for updates", "Không kiểm tra được"); break;
    case OTA_INSTALLING: head = TR("Installing ", "Đang cài ") + v + "… " + g_otaPct + "%"; sub = TR("Don’t turn off the Sticky. It restarts when done.", "Đừng tắt Sticky. Xong sẽ tự khởi động lại."); break;
    case OTA_FAILED: head = TR("Update failed", "Cập nhật thất bại"); sub = TR("This version is unchanged.", "Phiên bản hiện tại giữ nguyên."); btn = TR("Try again", "Thử lại"); break;
  }
  ui_text(F_B25, head, 40, 194, 33);
  ui_wrap(F_R25, sub, 40, 235, 400, 33, 2);  // one sentence, wrapped: Vietnamese runs longer than English
  if (update_has_button()) ui_button(40, UPD_BTN_Y, 400, 72, true, nullptr, btn);
}

// ---------- 04e Configuration (City is display only: set on the phone setup page) ----------
inline String units() { return cfg_get("units", "C"); }

inline void draw_config() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Configuration", "Cấu hình"), "");
  const String city = cfg_get("city");
  info_row(SROW_TOP, SROW_H, F_B28, TR("City", "Thành phố"), city.length() ? city : "—", false);
  info_row(SROW_TOP + SROW_H, SROW_H, F_B28, TR("Timezone", "Múi giờ"), utc_text(g_utc_off), false);
  info_row(SROW_TOP + 2 * SROW_H, SROW_H, F_B28, TR("Refresh interval", "Làm mới"), TR("30 min", "30 phút"), false);
  info_row(SROW_TOP + 3 * SROW_H, SROW_H, F_B28, TR("Temperature", "Nhiệt độ"), units() == "F" ? "°F" : "°C", true);
}

// ---------- 04g Temperature ----------
inline void checkbox(int x, int y, bool on) {  // Figma checkbox 36x36, 3 px, radius 4
  if (on) { display.fillRoundRect(x, y, 36, 36, 4, INK); ui_icon(ICON_CHECK26, x + 5, y + 5, PAPER); }
  else ui_card(x, y, 36, 36, 4, 3);
}

// 04b / 04c / 04g: one checkbox row per choice.
inline void draw_choices(const String &title, const std::vector<String> &rows, int sel) {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(title, "");
  for (int i = 0; i < int(rows.size()); i++) {
    const int y = SROW_TOP + i * SROW_H;
    ui_text(F_B28, ui_fit(F_B28, rows[i], 352), 40, y + 17, 37);
    checkbox(404, y + 18, i == sel);
    display.fillRect(40, y + SROW_H - 1, 400, 1, INK);
  }
}

inline void draw_temp() {
  draw_choices(TR("Temperature", "Nhiệt độ"), {TR("Celsius (°C)", "Độ C (°C)"), TR("Fahrenheit (°F)", "Độ F (°F)")}, units() == "F");
}

// ---------- 04m Sound (flash keys sound, vol): toggle in the top bar like Web setup; Off shows nothing below ----------
constexpr int VOL_N = 3;  // Low, Medium, High (hw.h VOLUME_DUTY)
inline void draw_sound() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_text(F_X32, TR("Sound", "Âm thanh"), 40, 54, 42);
  ui_toggle(372, 56, g_sound);
  display.fillRect(40, 109, 400, 3, INK);
  if (!g_sound) return;
  const String rows[VOL_N] = {TR("Low", "Nhỏ"), TR("Medium", "Vừa"), TR("High", "Lớn")};
  for (int i = 0; i < VOL_N; i++) {
    const int y = SROW_TOP + i * SROW_H;
    ui_text(F_B28, rows[i], 40, y + 17, 37);
    checkbox(404, y + 18, i == g_vol);
    display.fillRect(40, y + SROW_H - 1, 400, 1, INK);
  }
}

// ---------- 04l Sleep after (flash key sleep_min, 0 = Never) ----------
inline const int SLEEP_CHOICES[] = {1, 2, 5, 10, 0};
constexpr int SLEEP_N = sizeof SLEEP_CHOICES / sizeof SLEEP_CHOICES[0];
inline int g_sleepMin = 2;  // loaded at boot

inline void draw_sleep() {
  std::vector<String> rows;
  int sel = -1;
  for (int i = 0; i < SLEEP_N; i++) {
    const int m = SLEEP_CHOICES[i];
    rows.push_back(m ? String(m) + TR(" min", " phút") : TR("Never", "Không bao giờ"));
    if (m == g_sleepMin) sel = i;
  }
  draw_choices(TR("Sleep after", "Ngủ sau"), rows, sel);
}

// ---------- 04a Language & Font, 04b System Language, 04c User Font ----------
inline void draw_langfont() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Language & Font", "Ngôn ngữ & Phông"), "");
  info_row(SROW_TOP, SROW_H, F_B28, TR("System Language", "Ngôn ngữ hệ thống"), "", true);
  info_row(SROW_TOP + SROW_H, SROW_H, F_B28, TR("User Font", "Phông chữ"), "", true);
}

inline void draw_syslang() { draw_choices(TR("System Language", "Ngôn ngữ hệ thống"), {"English", "Tiếng Việt"}, g_vi); }

inline std::vector<String> g_fontNames;  // 04c rows after "Default", read when 04c opens
// sel: the row just tapped (ticked at once); loading: "Loading..." card while the font is read from the card.
inline void draw_userfont(int sel = -1, bool loading = false) {
  std::vector<String> rows = {TR("Default (built-in)", "Mặc định (có sẵn)")};
  rows.insert(rows.end(), g_fontNames.begin(), g_fontNames.end());
  const auto it = std::find(g_fontNames.begin(), g_fontNames.end(), cfg_get("font"));
  if (sel < 0) sel = it == g_fontNames.end() ? 0 : 1 + int(it - g_fontNames.begin());
  draw_choices(TR("User Font", "Phông chữ"), rows, sel);
  if (loading) {  // a card over the rows, so it fits however many fonts are listed
    ui_card(110, 352, 260, 96, 12, 3);
    ui_text(F_B25, TR("Loading...", "Đang tải..."), W / 2, 352 + (96 - 33) / 2, 33, AL_CENTER);
  }
}

// ---------- 04h About ----------
inline String about_sn() {
  uint8_t b[8] = {};
  esp_efuse_read_block(EFUSE_BLK3, b, 0, 64);  // user-data block, written at the factory
  return sn_text(b);
}

inline String sd_free_text() {
  if (!sd_inserted()) return TR("No card", "Không có thẻ");  // even if it was mounted before being pulled out
  if (!g_sdOk) return TR("Not mounted", "Chưa gắn");
  const String x = size_text(SD.totalBytes() - SD.usedBytes());
  return TR(x + " free", "Trống " + x);
}

constexpr int ABOUT_SD_ROW = 5;
inline void draw_about() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("About", "Thông tin"), "");
  // ponytail: certifications are a static line until the device label's text is confirmed (PLAN §12)
  const String rows[][2] = {{TR("Name", "Tên"), "HeySticky"}, {"SN", about_sn()}, {"MAC", WiFi.macAddress()},
                            {TR("Software version", "Phiên bản"), FW_VERSION}, {TR("Certifications", "Chứng nhận"), "FCC · CE"}, {TR("SD card", "Thẻ SD"), sd_free_text()}};
  for (int i = 0; i < 6; i++) info_row(SROW_TOP + i * IROW_H, IROW_H, F_B25, rows[i][0], rows[i][1], i == ABOUT_SD_ROW && sd_inserted());  // no card: no ›, no tap
}

// ---------- 04i SD Card ----------
constexpr int SD_FILES_ROW = 4;
inline void draw_sdcard() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("SD Card", "Thẻ SD"), "");
  const String space = g_sdOk ? size_text(SD.usedBytes()) + " / " + size_text(SD.totalBytes()) : "—";  // used / total
  info_row(SROW_TOP, IROW_H, F_B25, TR("Inserted", "Đã lắp"), sd_inserted() ? TR("Yes", "Có") : TR("No", "Không"), false);
  info_row(SROW_TOP + IROW_H, IROW_H, F_B25, TR("Mounted", "Đã gắn"), g_sdOk ? TR("Yes", "Có") : TR("No", "Không"), false);
  info_row(SROW_TOP + 2 * IROW_H, IROW_H, F_B25, TR("Path", "Đường dẫn"), "/sd", false);
  info_row(SROW_TOP + 3 * IROW_H, IROW_H, F_B25, TR("Space", "Dung lượng"), space, false);
  const String n = g_fileCount >= SD_FILES_MAX ? String(SD_FILES_MAX) + "+" : String(g_fileCount);
  info_row(SROW_TOP + 4 * IROW_H, IROW_H, F_B25, TR("Files", "Tệp"), g_sdOk ? n : "—", g_sdOk);
}

// ---------- 04k SD Files: one folder at a time (folders ›, then files with size) ----------
constexpr int FILES_ROWS = 9;
inline int g_filesTop = 0;  // first row shown; the list scrolls FILES_ROWS - 1 rows at a time
inline int files_max() { return max(0, int(g_files.size()) - FILES_ROWS); }

inline void draw_files() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(g_dir == "/" ? TR("SD Files", "Tệp trên thẻ SD") : sd_name(g_dir), String(g_files.size()));
  if (g_files.empty()) { ui_text(F_R25, TR("No files", "Không có tệp"), W / 2, 318, 33, AL_CENTER); return; }
  g_filesTop = constrain(g_filesTop, 0, files_max());
  for (int r = 0; r < FILES_ROWS; r++) {
    const int i = g_filesTop + r;
    if (i >= int(g_files.size())) break;
    const SdFile &f = g_files[i];
    const bool dir = f.items >= 0;
    const String val = dir ? String(f.items) : size_text(f.size);
    const String name = ui_fit(F_B25, dir ? f.name + "/" : f.name, 400 - ui_width(F_R25, val) - (dir ? 34 : 0) - 12);
    info_row(SROW_TOP + r * IROW_H, IROW_H, F_B25, name, val, dir);
  }
  if (files_max()) ui_scrollbar(SROW_TOP + 12, FILES_ROWS * IROW_H - 24, g_filesTop, FILES_ROWS, g_files.size());
}

// ---------- 04j Web setup ----------
static int s_qrX, s_qrY, s_qrS;
static void qr_modules(esp_qrcode_handle_t q) {
  const int n = esp_qrcode_get_size(q);
  s_qrS = 157 / n;  // whole pixels per module inside the 181 px frame
  const int x0 = 150 + (181 - n * s_qrS) / 2, y0 = s_qrY + (181 - n * s_qrS) / 2;
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(q, x, y)) display.fillRect(x0 + x * s_qrS, y0 + y * s_qrS, s_qrS, s_qrS, INK);
}

// "Wi-Fi  HeySticky-Setup": bold label + value, centred as one line.
static void kv_center(const String &k, const String &v, int y) {
  const int gap = 2 * ui_width(F_R25, " "), w = ui_width(F_B25, k) + gap + ui_width(F_R25, v), x = (W - w) / 2;
  ui_text(F_B25, k, x, y, 35);
  ui_text(F_R25, v, x + w - ui_width(F_R25, v), y, 35);
}

static void ws_qr(const String &s, int y) {  // 181 px framed QR, centred
  ui_card(150, y, 181, 181, 6, 2);
  s_qrY = y;
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = qr_modules;
  esp_qrcode_generate(&cfg, s.c_str());
}

static void ws_url(const String &ip, int y) {  // black box with the address
  display.fillRoundRect(40, y, 400, 87, 12, INK);
  ui_text(F_X42, ip, W / 2, y + 16, 55, AL_CENTER, PAPER, INK);
}

// Under the Hotspot | Home Wi-Fi choice (Figma 04j): 1 = the Home Wi-Fi warning before its QR, 2 = no Wi-Fi.
enum WsNote : uint8_t { WS_NONE, WS_WARN, WS_NOWIFI };
inline WsNote g_wsNote = WS_NONE;  // asked for by a tap; the hotspot keeps running meanwhile
inline WsNote ws_note() {  // Home Wi-Fi that lost its network shows "No Wi-Fi" too
  return g_wsNote ? g_wsNote : g_webOn && g_webHome && WiFi.status() != WL_CONNECTED ? WS_NOWIFI : WS_NONE;
}
inline bool ws_home() { return g_wsNote || g_webHome; }  // the choice shows Home Wi-Fi

// WebNotice: badge "!", title, body, centred between the choice (y 195) and the buttons (y 705).
static void draw_ws_note(WsNote n) {
  const bool warn = n == WS_WARN;
  const String title = warn ? TR("Open on home Wi-Fi?", "Mở trên Wi-Fi nhà?") : TR("No Wi-Fi connection", "Không có Wi-Fi");
  const String body = warn ? TR("Anyone on this Wi-Fi can open this page, and it isn’t encrypted. Use it only on a Wi-Fi you trust.",
                                "Ai dùng chung Wi-Fi này đều mở được trang này, và trang không được mã hoá. Chỉ dùng trên Wi-Fi bạn tin cậy.")
                           : TR("Set up Wi-Fi with Hotspot first.", "Hãy cài Wi-Fi bằng Điểm phát trước.");
  const int lines = ui_wrap(F_R25, body, 40, 0, 400, 35, 5, AL_CENTER, false);
  const int top = 195 + (705 - 195 - (160 + 18 + 42 + 18 + 35 * lines)) / 2;
  display.fillCircle(240, top + 80, 80, INK);  // Badge 160, 3 px ring
  display.fillCircle(240, top + 80, 77, PAPER);
  display.fillRoundRect(232, top + 36, 16, 60, 8, INK);  // "!"
  display.fillCircle(240, top + 116, 9, INK);
  ui_text(F_X32, title, W / 2, top + 178, 42, AL_CENTER);
  ui_wrap(F_R25, body, 40, top + 238, 400, 35, 5, AL_CENTER);
  if (warn) {
    ui_button(40, 705, 193, 79, false, nullptr, TR("Turn on", "Bật"));
    ui_button(247, 705, 193, 79, true, &ICON_X24, TR("Cancel", "Huỷ"));
  } else ui_button(40, 705, 400, 79, true, nullptr, TR("Use Hotspot", "Dùng Điểm phát"));
}

inline void draw_websetup(bool loading = false) {  // loading: toggle already On while the hotspot starts
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_text(F_X32, TR("Web setup", "Cài đặt qua web"), 40, 54, 42);
  ui_toggle(372, 56, g_webOn || loading);
  display.fillRect(40, 109, 400, 3, INK);
  if (loading) { ui_text(F_B25, TR("Loading...", "Đang tải..."), W / 2, (H - 33) / 2, 33, AL_CENTER); return; }
  if (!g_webOn) return;  // Off: nothing below the top bar, not even the choice
  const bool home = ws_home();
  ui_button(40, 128, 194, 67, !home, nullptr, TR("Hotspot", "Điểm phát"));
  ui_button(246, 128, 194, 67, home, nullptr, TR("Home Wi-Fi", "Wi-Fi nhà"));
  if (const WsNote n = ws_note()) return draw_ws_note(n);
  if (home) {  // the phone joins nothing: the QR opens the page on the home network
    const String ip = WiFi.localIP().toString();
    ui_text(F_B25, TR("Scan to open", "Quét để mở"), W / 2, 213, 33, AL_CENTER);
    ws_qr("http://" + ip + "/", 258);
    ui_text(F_B25, TR("Or open", "Hoặc mở"), W / 2, 451, 33, AL_CENTER);
    ws_url(ip, 496);
    const String q = "“" + WiFi.SSID() + "”";
    ui_wrap(F_R25, TR("Use the same Wi-Fi " + q + " to connect.", "Dùng cùng Wi-Fi " + q + " để kết nối."), 40, 595, 400, 35, 4, AL_CENTER);
    return;
  }
  ui_text(F_B25, TR("Scan to join", "Quét để kết nối"), W / 2, 213, 33, AL_CENTER);
  ws_qr(String("WIFI:T:WPA;S:") + AP_SSID + ";P:" + g_apPass + ";;", 258);  // the phone camera joins the hotspot
  kv_center("Wi-Fi", AP_SSID, 451);
  kv_center(TR("Password", "Mật khẩu"), g_apPass, 498);
  ui_text(F_B25, TR("Then open", "Rồi mở"), W / 2, 545, 33, AL_CENTER);
  ws_url(WiFi.softAPIP().toString(), 590);
  ui_text(F_R25, TR("It may open by itself.", "Trang có thể tự mở."), W / 2, 689, 35, AL_CENTER);
}
