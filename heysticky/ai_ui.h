// AI voice screens, Figma 05-05d. Drawing only; screens.h runs the flow.
#pragma once
#include "ui.h"
#include "mic.h"
#include "ai.h"
#include "src/icons.h"

// ---------- 05 Listening ----------
// Waveform strip (Figma `Waveform`, 260x80 at 110,503): 19 bars 8 px wide every 14 px, round ends.
constexpr int WAVE_X = 110, WAVE_Y = 503, WAVE_W = 260, WAVE_H = 80, WAVE_BARS = 19;
constexpr uint8_t WAVE_SHAPE[WAVE_BARS] = {10, 22, 40, 62, 48, 30, 70, 56, 34, 18, 44, 66, 38, 24, 52, 30, 14, 26, 12};  // Step=1
constexpr float WAVE_DB_FLOOR = -62, WAVE_DB_FULL = -32;  // calibration knobs: mean level (dBFS) shown flat / full height
constexpr uint32_t WAVE_EVERY_MS = 350;                    // Figma: one step every 0.35 s (a partial takes ~860 ms anyway)

// 0..1 from the loudest mean level since the last call (flat when silent).
inline float wave_level() {
  const int v = g_micLevelMax;
  g_micLevelMax = 0;
  const float db = 20 * log10f(max(v, 1) / 32768.0f);
  return constrain((db - WAVE_DB_FLOOR) / (WAVE_DB_FULL - WAVE_DB_FLOOR), 0.0f, 1.0f);
}

// The Figma shape scaled by the level and shifted each step so the bars move; 8 px dots when silent.
inline void draw_wave(float level, int step) {
  display.fillRect(WAVE_X, WAVE_Y, WAVE_W, WAVE_H, PAPER);
  for (int i = 0; i < WAVE_BARS; i++) {
    const int h = max(8, int(WAVE_SHAPE[(i + step * 5) % WAVE_BARS] * level));
    display.fillRoundRect(WAVE_X + i * 14, WAVE_Y + (WAVE_H - h) / 2, 8, h, 4, INK);
  }
}

inline void draw_listen() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_text(F_X40, TR("Listening…", "Đang nghe…"), W / 2, 241, 48, AL_CENTER);  // Figma: ExtraBold 36; nearest built-in is 40
  display.fillCircle(240, 396, 85, INK);                                       // Badge 170 at 155,311
  ui_icon(ICON_MIC85, 198, 354, PAPER);                                        // icon/mic 85 at 197.5,353.5
  draw_wave(0, 0);
}

inline String clip_chars(const String &s, unsigned n) {  // first n UTF-8 characters, "…" when cut
  unsigned c = 0, i = 0;
  for (; i < s.length(); i++)
    if ((uint8_t(s[i]) & 0xC0) != 0x80 && c++ == n) break;
  return i < s.length() ? s.substring(0, i) + "…" : s;
}

// ---------- 05a Working: badge with moving dots, title, three steps (Figma 05a, content at 40,228) ----------
inline int g_aiStep = 1;  // the step in progress: 1 sorting (Heard you is done on arrival), 2 saving, 3 all done

constexpr int WORK_Y = 463, WORK_H = 133;  // the steps
constexpr int DOTS_X = 205, DOTS_Y = 296, DOTS_W = 70, DOTS_H = 14;  // the dots inside the badge (centre 240,303)

inline void draw_working_dots(int frame) {  // 1, 2, 3 dots, again: "still working" on a 1-bit panel
  display.fillRect(DOTS_X, DOTS_Y, DOTS_W, DOTS_H, PAPER);
  for (int k = 0; k <= frame % 3; k++) display.fillCircle(218 + 22 * k, 303, 3, INK);
}

inline void draw_working_steps() {
  display.fillRect(40, WORK_Y, 400, WORK_H, PAPER);
  static const char *const EN[] = {"Heard you", "Sorting it out…", "Saving…"}, *const VI[] = {"Đã nghe", "Đang phân loại…", "Đang lưu…"};
  for (int i = 0; i < 3; i++) {
    const int y = WORK_Y + 10 + 45 * i, cy = y + 16;
    display.fillCircle(55, cy, 15, INK);  // done: black disc + white check; in progress: ring + dots; later: ring
    if (i < g_aiStep) ui_icon(ICON_CHECK20, 45, cy - 10, PAPER);
    else {
      display.fillCircle(55, cy, 12, PAPER);
      if (i == g_aiStep)
        for (int dx : {-6, 0, 6}) display.fillCircle(55 + dx, cy, 1, INK);
    }
    ui_text(i == g_aiStep ? F_X25 : F_B25, TR(EN[i], VI[i]), 82, y, 33);
  }
}

inline void draw_working() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  display.fillCircle(240, 303, 75, INK);  // Badge 150 at 165,228, 5 px ring
  display.fillCircle(240, 303, 70, PAPER);
  draw_working_dots(2);
  ui_text(F_X32, TR("Working on it…", "Đang xử lý…"), W / 2, 398, 45, AL_CENTER);  // Figma: ExtraBold 34
  draw_working_steps();
}

// ---------- 05b Review: the AI was not sure; the transcript is saved as it is, as a to-do or a note ----------
inline String g_aiText;     // what was heard
inline float g_aiSecs = 0;  // how long the take was

inline bool looks_vi(const String &s) {  // ponytail: any non-ASCII letter = Vietnamese; enough while the Sticky speaks EN/VI
  for (const char c : s)
    if (uint8_t(c) >= 0x80) return true;
  return false;
}

inline void draw_review() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_text(F_X32, TR("Here’s what I heard", "Tôi đã nghe được"), 40, 52, 42);
  ui_text(F_R25, TR("Not sure. Save it as:", "Chưa rõ. Lưu thành:"), 40, 104, 33);
  const String q = "“" + clip_chars(g_aiText, 150) + "”";
  const int n = ui_wrap(F_B26, q, 63, 0, 354, 35, 6, AL_LEFT, false);
  ui_card(40, 147, 400, 21 + 35 * n + 12 + 33 + 21, 14, 3);
  ui_wrap(F_B26, q, 63, 168, 354, 35, 6);
  const int s = int(g_aiSecs + 0.5f);
  char t[8];
  snprintf(t, sizeof t, "%d:%02d", s / 60, s % 60);
  ui_text(F_R25, String(t) + "  ·  " + (looks_vi(g_aiText) ? TR("Vietnamese", "Tiếng Việt") : TR("English", "Tiếng Anh")), 63, 168 + 35 * n + 12, 33);
  ui_card(40, 619, 193, 72, 14, 3);
  ui_icon(ICON_CHECKLIST34, 59, 638);
  ui_text(F_X25, TR("To-do", "Việc"), 103, 638, 33);
  ui_card(247, 619, 193, 72, 14, 3);
  ui_icon(ICON_NOTE34, 266, 638);
  ui_text(F_X25, TR("Note", "Ghi chú"), 310, 638, 33);
  ui_button(40, 705, 400, 79, false, &ICON_RETRY26, TR("Record again?", "Ghi âm lại?"), 10);
}

// ---------- 05c Done: what was added; Go to To-do / Notes opens that list ----------
inline std::vector<String> g_doneItems;  // to-do texts, or the note's title
inline bool g_doneNote = false;

inline void draw_done() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  const int shown = min<int>(g_doneItems.size(), 3), more = int(g_doneItems.size()) - shown;
  const int cardH = 19 + 33 + 6 + 42 * shown + (more ? 33 : 0) + 19;
  const int top = 40 + (705 - 40 - (160 + 18 + 58 + 18 + cardH)) / 2;  // content centred between the bar and the buttons
  display.fillCircle(240, top + 80, 80, INK);  // Badge 160, black
  ui_icon(ICON_CHECK80, 200, top + 40, PAPER);
  ui_text(F_X40, TR("Added", "Đã thêm"), W / 2, top + 178, 58, AL_CENTER);  // Figma: ExtraBold 44
  const int cy = top + 254;
  ui_card(40, cy, 400, cardH, 14, 3);
  ui_text(F_B25, g_doneNote ? TR("NOTES", "GHI CHÚ") : TR("TO-DOS LIST", "DANH SÁCH VIỆC"), W / 2, cy + 19, 33, AL_CENTER);
  for (int i = 0; i < shown; i++) ui_text(F_X32, ui_fit(F_X32, g_doneItems[i], 360), W / 2, cy + 58 + 42 * i, 42, AL_CENTER);
  if (more) ui_text(F_B25, "+" + String(more) + TR(" more", " việc nữa"), W / 2, cy + 58 + 42 * shown, 33, AL_CENTER);
  ui_button(40, 705, 193, 79, true, nullptr, g_doneNote ? TR("Go to Notes", "Mở Ghi chú") : TR("Go to To-do", "Mở Việc"));
  ui_button(247, 705, 193, 79, false, nullptr, TR("Done", "Xong"));
}

// ---------- 05d Failed: why, and Try again / Home ----------
inline AiFail g_failKind = AF_NONE;
inline int g_failCode = 0;

inline void fail_text(String &head, String &body) {
  const bool key = g_failCode == 401 || g_failCode == 403;
  switch (g_failKind) {
    case AF_NONET:
      head = TR("No internet connection", "Không có Internet");
      body = TR("Your request was not sent. Check Wi-Fi in Settings, then try again.", "Yêu cầu chưa được gửi. Kiểm tra Wi-Fi trong Cài đặt rồi thử lại.");
      return;
    case AF_NOKEY:
      head = TR("AI key missing", "Thiếu khoá AI");
      body = TR("Open Web setup and add your API key in Integrations › AI.", "Mở Cài đặt qua web và thêm khoá API trong Tích hợp › AI.");
      return;
    case AF_SHORT:
      head = TR("I didn’t catch that", "Tôi chưa nghe rõ");
      body = TR("Hold the top button while you speak, then let go.", "Giữ nút trên trong khi nói, rồi thả ra.");
      return;
    case AF_EMPTY:
      head = TR("I didn’t catch that", "Tôi chưa nghe rõ");
      body = TR("No clear words came through. Speak a little closer, then try again.", "Không nghe ra từ nào rõ ràng. Nói gần hơn một chút rồi thử lại.");
      return;
    default:
      head = g_failCode > 0 ? String(TR("AI service error (", "Lỗi dịch vụ AI (")) + g_failCode + ")" : String(TR("AI service error", "Lỗi dịch vụ AI"));
      if (key) body = TR("The service refused the API key. Check it in Web setup.", "Dịch vụ từ chối khoá API. Kiểm tra lại trong Cài đặt qua web.");
      else if (g_failCode <= 0) body = TR("No answer from the service. Try again in a moment.", "Dịch vụ không trả lời. Thử lại sau giây lát.");
      else if (g_failKind == AF_STT) body = TR("Speech-to-text didn’t work. Try again in a moment.", "Chuyển giọng nói thành chữ không được. Thử lại sau giây lát.");
      else if (g_failKind == AF_REPLY) body = TR("The AI’s answer couldn’t be read. Try again.", "Không đọc được câu trả lời của AI. Thử lại.");
      else body = TR("The AI didn’t answer. Try again in a moment.", "AI không trả lời. Thử lại sau giây lát.");
  }
}

inline void draw_failed() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  String head, body;
  fail_text(head, body);
  const int n = ui_wrap(F_R25, body, 63, 0, 354, 35, 5, AL_LEFT, false);
  const int cardH = 19 + 33 + 6 + 35 * n + 19;
  const int top = 40 + (705 - 40 - (160 + 18 + 53 + 18 + cardH)) / 2;
  display.fillCircle(240, top + 80, 80, INK);  // Badge 160, 5 px ring
  display.fillCircle(240, top + 80, 75, PAPER);
  ui_icon(ICON_X80, 200, top + 40);
  ui_text(F_X40, TR("Couldn’t do that", "Không làm được"), W / 2, top + 178, 53, AL_CENTER);
  const int cy = top + 249;
  ui_card(40, cy, 400, cardH, 14, 3);
  ui_text(F_X25, ui_fit(F_X25, head, 354), 63, cy + 19, 33);
  ui_wrap(F_R25, body, 63, cy + 58, 354, 35, 5);
  ui_button(40, 705, 193, 79, true, nullptr, TR("Try again", "Thử lại"));
  ui_button(247, 705, 193, 79, false, nullptr, TR("Home", "Trang chủ"));
}
