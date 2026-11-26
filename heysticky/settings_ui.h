// Settings screens, Figma 04-04u: Settings, Configuration, Temperature, About, SD Card, SD Files, Web setup, Report a bug. Drawing and hit areas only; screens.h navigates.
#pragma once
#include <qrcode.h>  // esp_qrcode, bundled with the ESP32 core
#include <esp_efuse.h>
#include "ui.h"
#include "card.h"
#include "net.h"
#include "ota.h"
#include "settings.h"
#include "report.h"

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
enum SetRow : uint8_t { SR_ROTATE, SR_SOUND, SR_LANG, SR_UPDATE, SR_CONFIG, SR_SLEEP, SR_WEB, SR_BUG, SR_ABOUT, SR_COUNT };
inline const struct { const Icon *icon; const char *label, *vi; } SET_ROWS[SR_COUNT] = {
  {&ICON_SET_ROTATE, "Auto-rotate", "Tự xoay"}, {&ICON_SET_SOUND, "Sound", "Âm thanh"},
  {&ICON_SET_LANG, "Language & Font", "Ngôn ngữ & Phông"}, {&ICON_SET_UPDATE, "Software Update", "Cập nhật phần mềm"},
  {&ICON_SET_CONFIG, "Configuration", "Cấu hình"}, {&ICON_SET_SLEEP, "Auto-sleep", "Tự ngủ"}, {&ICON_SET_WEB, "Web setup", "Cài đặt qua web"},
  {&ICON_SET_BUG, "Report a bug", "Báo lỗi"}, {&ICON_SET_ABOUT, "About", "Thông tin"},
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

// ---------- 04l Auto-sleep (flash key sleep_min, 0 = Never) ----------
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
  draw_choices(TR("Auto-sleep", "Tự ngủ"), rows, sel);
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
static int s_qrY, s_qrW, s_qrIn;
static void qr_modules(esp_qrcode_handle_t q) {
  const int n = esp_qrcode_get_size(q), m = s_qrIn / n;  // whole pixels per module inside the frame
  const int x0 = (W - n * m + 1) / 2, y0 = s_qrY + (s_qrW - n * m) / 2;
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(q, x, y)) display.fillRect(x0 + x * m, y0 + y * m, m, m, INK);
}

// "Wi-Fi  HeySticky-Setup": bold label + value, centred as one line.
static void kv_center(const String &k, const String &v, int y) {
  const int gap = 2 * ui_width(F_R25, " "), w = ui_width(F_B25, k) + gap + ui_width(F_R25, v), x = (W - w) / 2;
  ui_text(F_B25, k, x, y, 35);
  ui_text(F_R25, v, x + w - ui_width(F_R25, v), y, 35);
}

// Framed QR, centred: 181 px for 04j, 260 px for 04p / 04q (a report link needs up to version ~16).
static void ws_qr(const String &s, int y, int size = 181, int inner = 157, int maxVer = 10) {
  ui_card((W - size + 1) / 2, y, size, size, 6, 2);
  s_qrY = y;
  s_qrW = size;
  s_qrIn = inner;
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = qr_modules;
  cfg.max_qrcode_version = maxVer;
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

// ---------- 04n-04u Report a bug (report.h) ----------
inline Report g_bug;         // the report being made: a new ID per draft, the AI's title ("" when written on the phone)
inline String g_bugBody;     // the AI's text
inline std::vector<Report> g_reports;  // 04u, read when it opens

inline std::vector<Report> reports_load() { return reports_parse(cfg_get("reports")); }
inline void reports_save(const std::vector<Report> &v) { cfg_set("reports", reports_text(v)); }
inline void report_keep(Report r) {  // 04p Done, or the phone's Report on GitHub: 04u follows it
  r.at = time(nullptr) >= CLOCK_OK ? uint32_t(time(nullptr)) : 0;
  auto v = reports_load();
  reports_add(v, r);
  reports_save(v);
}
inline void bug_draft(const String &title = "", const String &body = "") {
  g_bug = {report_id(esp_random()), title, 'w'};
  g_bugBody = body;
}

// States from GitHub's issue search (no token: 10 searches a minute, REPORT_ORS + 1 IDs each); after a full
// search, reports never sent drop off. Saved if changed.
inline bool reports_refresh(std::vector<Report> &v) {
  if (v.empty() || WiFi.status() != WL_CONNECTED) return false;
  JsonDocument f;
  report_filter(f);
  bool changed = false, all = true;
  for (size_t i = 0; i < v.size(); i += REPORT_ORS + 1) {
    JsonDocument d;
    if (!http_json(report_search_url(v, i, REPORT_ORS + 1), d, &f)) { all = false; break; }  // offline or rate-limited: keep the last states
    changed |= reports_apply(v, i, REPORT_ORS + 1, d["items"]);
  }
  if (all) changed |= reports_prune(v, uint32_t(time(nullptr)));
  if (changed) reports_save(v);
  return changed;
}

inline String report_state(char st) {
  switch (st) {
    case 'o': return TR("Open", "Đang mở");
    case 'f': return TR("Fixed", "Đã sửa");
    case 'x': return TR("Won’t fix", "Không sửa");
    default: return TR("Waiting", "Đang chờ");
  }
}

// 04n: Create a report, My reports.
inline void draw_bugmenu() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Report a bug", "Báo lỗi"), "");
  const Icon *icons[2] = {&ICON_BUG_NEW, &ICON_BUG_LIST};
  const String labels[2] = {TR("Create a report", "Tạo báo cáo"), TR("My reports", "Báo cáo của tôi")};
  for (int i = 0; i < 2; i++) {
    const int y = SROW_TOP + i * SROW_H;
    ui_icon(*icons[i], 40, y + 20);
    ui_text(F_B28, labels[i], 86, y + 17, 37);
    ui_icon(ICON_CHEV, 414, y + 23);
    display.fillRect(40, y + SROW_H - 1, 400, 1, INK);
  }
}

// 04t: three cards; Tell the AI is the top button held (no tap), GitHub / Email open their QR.
constexpr int BUGOPT_Y = 128, BUGOPT_STEP = 176, BUGOPT_H = 160;
inline void draw_bugnew() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Create a report", "Tạo báo cáo"), "");
  const String head[3] = {TR("Tell the AI", "Nói với AI"), "GitHub", "Email"};
  const String sub[3] = {TR("Hold the top button and say what went wrong.", "Giữ nút trên cùng và nói lỗi gặp phải."),
                         TR("Write it on GitHub with your phone.", "Viết trên GitHub bằng điện thoại."),
                         TR("Write an email from your phone.", "Viết email từ điện thoại.")};
  for (int i = 0; i < 3; i++) {
    const int y = BUGOPT_Y + i * BUGOPT_STEP;
    ui_card(40, y, 400, BUGOPT_H, 14, 3);
    ui_text(F_X30, head[i], 64, y + 26, 40);
    ui_wrap(F_R25, sub[i], 64, y + 72, i ? 320 : 352, 33, 2);
    if (i) ui_icon(ICON_CHEV, 394, y + 67);
  }
  ui_text(F_R25, TR("Your phone needs internet.", "Điện thoại cần có internet."), W / 2, 664, 33, AL_CENTER);
}

// 04p GitHub / 04q Email: the link as a 260 px QR, the report ID, Done.
inline void draw_bugqr(bool mail) {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(mail ? "Email" : "GitHub", "");
  ui_text(F_B25, mail ? TR("Scan to report by email", "Quét để báo lỗi qua email") : TR("Scan to report on GitHub", "Quét để báo lỗi trên GitHub"),
          W / 2, 130, 33, AL_CENTER);
  ws_qr(mail ? report_mail_url(g_bug, g_bugBody) : report_gh_url(g_bug, g_bugBody), 175, 260, 244, 25);
  if (mail) {
    ui_text(F_R25, REPORT_MAIL, W / 2, 455, 33, AL_CENTER);
    ui_wrap(F_R25, TR("Your mail app opens with the report filled in. Tap Send.",
                      "Ứng dụng email mở với báo cáo đã điền sẵn. Bấm Gửi."), 40, 500, 400, 33, 3, AL_CENTER);
  } else {
    ui_wrap(F_R25, TR("Sign in to GitHub on your phone, check the text, then tap Create.",
                      "Đăng nhập GitHub trên điện thoại, kiểm tra nội dung, rồi bấm Create."), 40, 455, 400, 33, 3, AL_CENTER);
  }
  ui_text(F_R20, TR("Report ", "Báo cáo ") + g_bug.id, W / 2, mail ? 645 : 600, 26, AL_CENTER);
  ui_button(40, 705, 400, 79, true, nullptr, TR("Done", "Xong"));
}

// 04r: what the AI wrote (in English), then GitHub | Email, or Try again.
inline void draw_bugreview() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_text(F_X32, TR("Your bug report", "Báo cáo lỗi của bạn"), 40, 52, 42);
  ui_text(F_R25, TR("Check it, then send.", "Kiểm tra, rồi gửi."), 40, 104, 33);
  ui_card(40, 147, 400, 300, 14, 3);
  ui_text(F_B26, ui_fit(F_B26, ui_drawable(F_B26, g_bug.title), 354), 63, 166, 33);
  ui_wrap(F_R25, ui_drawable(F_R25, g_bugBody), 63, 209, 354, 33, 6);
  ui_wrap(F_R25, TR("Posted publicly on GitHub. Don’t say personal info.", "Đăng công khai trên GitHub. Đừng nói thông tin cá nhân."), 40, 470, 400, 33, 2);
  ui_text(F_B25, TR("Send it with", "Gửi qua"), 40, 576, 33);
  ui_button(40, 619, 193, 72, false, nullptr, "GitHub");
  ui_button(247, 619, 193, 72, false, nullptr, "Email");
  ui_button(40, 705, 400, 79, false, nullptr, TR("Try again", "Thử lại"));
}

// 04u: title left, state right; the note explains Waiting.
inline void draw_buglist() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("My reports", "Báo cáo của tôi"), "");
  if (g_reports.empty()) { ui_text(F_R25, TR("No reports yet", "Chưa có báo cáo"), W / 2, 430, 33, AL_CENTER); return; }
  bool waiting = false;
  for (size_t i = 0; i < g_reports.size(); i++) {
    const Report &r = g_reports[i];
    const String st = report_state(r.st);
    const String title = ui_drawable(F_B25, r.title.length() ? r.title : r.id);
    info_row(SROW_TOP + i * IROW_H, IROW_H, F_B25, ui_fit(F_B25, title, 400 - ui_width(F_R25, st) - 12), st, false);
    waiting |= r.st == 'w';
  }
  if (waiting) ui_text(F_R20, TR("Waiting: not on GitHub yet.", "Đang chờ: chưa có trên GitHub."), W / 2, 735, 26, AL_CENTER);
}
