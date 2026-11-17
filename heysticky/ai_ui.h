// AI voice screens, Figma 05-05d. Drawing only; screens.h runs the flow.
#pragma once
#include "ui.h"
#include "mic.h"
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
