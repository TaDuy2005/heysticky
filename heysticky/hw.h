// HeySticky hardware: buttons, touch (GT911), buzzer, SHT40, BQ27220 battery gauge.
// Raw I2C, no extra libraries.
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <time.h>
#include <sys/time.h>
#include "pins.h"
#include "todo.h"  // day_num(): calendar date -> days, for the RTC

// ---------- buzzer ----------
// Driven with LEDC PWM: the duty sets the volume (50 % is the loudest one pin can drive a passive buzzer),
// a one-shot timer ends each sound, so beeps never block and never stretch during an e-paper refresh.
#include <esp_timer.h>
inline bool g_sound = false;  // Settings › Sound (Preferences "sound"), off by default
inline uint8_t g_vol = 2;     // Settings › Sound › Volume (Preferences "vol"): 0 Low, 1 Medium, 2 High
constexpr uint16_t VOLUME_DUTY[] = {40, 150, 512};  // calibration knob: duty out of 1024 per level
inline void hw_buzz(int hz, int ms) {  // plays even with Sound off: callers decide
  static esp_timer_handle_t stop = nullptr;
  if (!stop) {
    ledcAttach(PIN_BUZZER, hz, 10);
    const esp_timer_create_args_t a = {.callback = [](void *) { ledcWrite(PIN_BUZZER, 0); }, .name = "buzz"};
    esp_timer_create(&a, &stop);
  } else {
    esp_timer_stop(stop);
    ledcChangeFrequency(PIN_BUZZER, hz, 10);
  }
  ledcWrite(PIN_BUZZER, VOLUME_DUTY[min<int>(g_vol, 2)]);
  esp_timer_start_once(stop, ms * 1000ULL);
}
constexpr int BEEP_MS = 120;  // calibration knob: every tap / swipe / button beep (long enough to sound full)
inline void hw_beep(int ms = BEEP_MS, int hz = 2700) { if (g_sound) hw_buzz(hz, ms); }

// Power tunes: they confirm a hold worked, so they play even with Sound off (at the chosen volume). Pitched 1.3-4.2 kHz (loud on this buzzer).
struct Tone { uint16_t hz, ms; };
constexpr Tone TUNE_ON[] = {{2093, 70}, {2637, 70}, {3136, 70}, {4186, 180}};  // quick rising C-E-G-C
constexpr Tone TUNE_OFF[] = {{2637, 160}, {1976, 160}, {1319, 360}};          // slow falling E-B-E
template <size_t N> inline void hw_tune(const Tone (&t)[N]) {
  for (const Tone &n : t) { hw_buzz(n.hz, n.ms); delay(n.ms + 25); }
}

// ---------- buttons ----------
enum HwButton : uint8_t { BTN_NONE, BTN_TOP, BTN_UP, BTN_DOWN, BTN_TOP_LONG };
constexpr uint32_t LONG_PRESS_MS = 600;  // calibration knob: hold-to-talk threshold

struct BtnState { int pin; int idle; bool down; uint32_t since; bool longSent; };
inline BtnState g_btn[3] = {{PIN_BTN_TOP}, {PIN_BTN_UP}, {PIN_BTN_DOWN}};
inline bool g_chord = false;  // UP + DOWN pressed together (the shutdown hold): neither release pages

inline void hw_buttons_begin() {
  for (auto &b : g_btn) {
    pinMode(b.pin, INPUT_PULLUP);
    b.idle = HIGH;  // measured: all three read 1 released (a button held at boot must not flip this)
  }
}

// Returns one event per call: short presses fire on release, TOP_LONG fires while held.
inline HwButton hw_buttons_poll() {
  const uint32_t now = millis();
  for (int i = 0; i < 3; i++) {
    BtnState &b = g_btn[i];
    bool pressed = digitalRead(b.pin) != b.idle;
    if (pressed && !b.down) { b.down = true; b.since = now; b.longSent = false; }
    else if (pressed && b.down && i == 0 && !b.longSent && now - b.since >= LONG_PRESS_MS) {
      b.longSent = true; return BTN_TOP_LONG;
    } else if (!pressed && b.down) {
      b.down = false;
      if (now - b.since < 30 || b.longSent) continue;  // debounce / already reported as long
      if (i && (g_chord || g_btn[3 - i].down)) { g_chord = g_btn[3 - i].down; continue; }  // UP + DOWN: no paging
      return HwButton(BTN_TOP + i);
    }
  }
  return BTN_NONE;
}

// ---------- I2C helpers (16-bit register for GT911, 8-bit for the rest) ----------
inline bool i2c_read(TwoWire &w, uint8_t addr, uint16_t reg, bool reg16, uint8_t *buf, size_t n) {
  w.beginTransmission(addr);
  if (reg16) w.write(reg >> 8);
  w.write(reg & 0xFF);
  if (w.endTransmission(false) != 0) return false;
  if (w.requestFrom(addr, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = w.read();
  return true;
}

inline bool i2c_write(TwoWire &w, uint8_t addr, uint8_t reg, const uint8_t *buf, size_t n) {
  w.beginTransmission(addr);
  w.write(reg);
  w.write(buf, n);
  return w.endTransmission() == 0;
}

// ---------- PCF8563 RTC (Wire1): keeps UTC on the battery while the Sticky is off ----------
// The factory firmware uses a PCF8563; 0x51 is that chip's fixed address. Years 2000-2099 (century bit ignored).
constexpr uint8_t RTC_ADDR = 0x51;
inline uint8_t from_bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
inline uint8_t to_bcd(int v) { return uint8_t((v / 10) << 4 | (v % 10)); }

inline bool hw_rtc_read(time_t &t) {  // false: no answer, or its voltage-low flag says the time was lost
  uint8_t r[7];
  if (!i2c_read(Wire1, RTC_ADDR, 0x02, false, r, 7) || (r[0] & 0x80)) return false;
  const int y = 2000 + from_bcd(r[6]);
  t = time_t(day_num(y, from_bcd(r[5] & 0x1F), from_bcd(r[3] & 0x3F)) * 86400LL +
             from_bcd(r[2] & 0x3F) * 3600 + from_bcd(r[1] & 0x7F) * 60 + from_bcd(r[0] & 0x7F));
  return y > 2016;
}

inline bool hw_rtc_write(time_t t) {  // also clears the voltage-low flag
  tm x;
  gmtime_r(&t, &x);
  const uint8_t r[7] = {to_bcd(x.tm_sec), to_bcd(x.tm_min), to_bcd(x.tm_hour), to_bcd(x.tm_mday), uint8_t(x.tm_wday),
                        to_bcd(x.tm_mon + 1), to_bcd(x.tm_year % 100)};
  return i2c_write(Wire1, RTC_ADDR, 0x02, r, 7);
}

// System clock from the RTC (boot, and each wake: light sleep's own clock drifts). Only a change of 2 s or more is applied.
inline bool hw_rtc_to_clock() {
  time_t t;
  if (!hw_rtc_read(t)) return false;
  if (llabs(int64_t(t) - int64_t(time(nullptr))) >= 2) { timeval tv = {t, 0}; settimeofday(&tv, nullptr); }
  return true;
}

// ---------- touch (GT911 on Wire) ----------
inline uint8_t g_tp_addr = 0;
inline uint16_t g_tp_maxX = 0, g_tp_maxY = 0;

inline bool hw_touch_begin() {
  pinMode(PIN_TP_EN, OUTPUT); digitalWrite(PIN_TP_EN, HIGH);
  // Reset with INT low selects address 0x5D.
  pinMode(PIN_TP_RST, OUTPUT); pinMode(PIN_TP_INT, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW); digitalWrite(PIN_TP_INT, LOW); delay(20);
  digitalWrite(PIN_TP_RST, HIGH); delay(60);
  pinMode(PIN_TP_INT, INPUT);
  Wire.begin(PIN_TP_SDA, PIN_TP_SCL, 400000);
  for (uint8_t a : {0x5D, 0x14}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { g_tp_addr = a; break; }
  }
  if (!g_tp_addr) return false;
  uint8_t id[4] = {0}, cfg[4];
  i2c_read(Wire, g_tp_addr, 0x8140, true, id, 4);
  if (i2c_read(Wire, g_tp_addr, 0x8048, true, cfg, 4)) {
    g_tp_maxX = cfg[0] | cfg[1] << 8; g_tp_maxY = cfg[2] | cfg[3] << 8;
  }
  Serial.printf("[touch] GT911 id=%.4s addr=0x%02X res=%ux%u\n", (char *)id, g_tp_addr, g_tp_maxX, g_tp_maxY);
  return true;
}

// One GT911 report: a point, an explicit release (report with 0 points), or nothing new.
// ponytail: first point only, multi-touch when a screen needs it.
enum TpState : uint8_t { TP_NONE, TP_POINT, TP_RELEASE };
inline TpState hw_touch_read(uint16_t &x, uint16_t &y) {
  if (!g_tp_addr) return TP_NONE;
  uint8_t st;
  if (!i2c_read(Wire, g_tp_addr, 0x814E, true, &st, 1) || !(st & 0x80)) return TP_NONE;
  TpState r = TP_RELEASE;
  if (st & 0x0F) {
    uint8_t p[5];
    if (i2c_read(Wire, g_tp_addr, 0x814F, true, p, 5)) { x = p[1] | p[2] << 8; y = p[3] | p[4] << 8; r = TP_POINT; }
  }
  Wire.beginTransmission(g_tp_addr);  // clear buffer-ready flag
  Wire.write(0x81); Wire.write(0x4E); Wire.write(0);
  Wire.endTransmission();
  return r;
}

// Touch in screen coordinates (portrait 480x800, rotation 1).
// Calibration knobs, checked on the device with a dot test: only Y is flipped.
constexpr bool TOUCH_FLIP_X = false, TOUCH_FLIP_Y = true;
inline TpState hw_touch_get(int16_t &x, int16_t &y) {
  uint16_t rx, ry;
  const TpState s = hw_touch_read(rx, ry);
  if (s != TP_POINT) return s;
  const int16_t w = g_tp_maxX ? g_tp_maxX : 480, h = g_tp_maxY ? g_tp_maxY : 800;
  x = constrain(TOUCH_FLIP_X ? w - 1 - rx : rx, 0, w - 1);
  y = constrain(TOUCH_FLIP_Y ? h - 1 - ry : ry, 0, h - 1);
  return TP_POINT;
}

// ---------- touch task: keeps reading while the main loop waits on an e-paper refresh ----------
struct TouchEv { int16_t x, y; bool down; };  // down = finger landed, else lifted (x/y = last point)
inline QueueHandle_t g_touchQ = nullptr;
inline volatile bool g_touchHeld = false;     // finger currently on the glass
constexpr uint32_t TOUCH_LOST_MS = 300;       // fallback release if the chip never reports 0 points
constexpr int TOUCH_NEW_TAP_PX = 40;          // a jump this far within one contact = a new tap (fast typing)
inline volatile bool g_touchOff = false;      // asked by sleep (power.h): GT911 powered down
inline volatile bool g_touchParked = false;   // the task has powered it down and stopped reading

inline void touch_task(void *) {
  bool down = false;
  int16_t lx = 0, ly = 0;
  uint32_t last = 0;
  auto push = [](int16_t x, int16_t y, bool d) { TouchEv e{x, y, d}; xQueueSend(g_touchQ, &e, 0); };
  for (;;) {
    if (g_touchOff != g_touchParked) {  // power the GT911 down for sleep, or back up with a fresh reset
      if (g_touchOff) digitalWrite(PIN_TP_EN, LOW);
      else hw_touch_begin();
      down = false;
      g_touchParked = g_touchOff;
    }
    if (g_touchParked) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    int16_t x, y;
    const TpState s = hw_touch_get(x, y);
    if (s == TP_POINT) {
      if (down && (abs(x - lx) > TOUCH_NEW_TAP_PX || abs(y - ly) > TOUCH_NEW_TAP_PX) && millis() - last > 30) {
        push(lx, ly, false);  // next finger landed before the chip reported the lift
        down = false;
      }
      if (!down) { down = true; push(x, y, true); }
      lx = x; ly = y; last = millis();
    } else if (down && (s == TP_RELEASE || millis() - last > TOUCH_LOST_MS)) {
      down = false;
      push(lx, ly, false);
    }
    g_touchHeld = down;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ---------- SHT40 (0x44 on Wire1) ----------
inline bool hw_sht40_read(float &tempC, float &rh) {
  Wire1.beginTransmission(0x44);
  Wire1.write(0xFD);  // measure, high precision
  if (Wire1.endTransmission() != 0) return false;
  delay(10);
  uint8_t b[6];
  if (Wire1.requestFrom((uint8_t)0x44, (uint8_t)6) != 6) return false;
  for (auto &v : b) v = Wire1.read();
  tempC = -45 + 175.0f * (b[0] << 8 | b[1]) / 65535.0f;
  rh = constrain(-6 + 125.0f * (b[3] << 8 | b[4]) / 65535.0f, 0.0f, 100.0f);
  return true;
}

// ---------- BQ27220 (0x55 on Wire1) ----------
// The factory setup runs the gauge at 10x scale: its design / full-charge capacity read 7500 mAh for the
// 750 mAh cell, so its current is 10x too. Measured 2026-11-17: SoC rose ~0.7 %/min at "3000 mA" = 300 mA real.
constexpr int GAUGE_SCALE = 10;  // calibration knob: gauge mA / real mA
struct Battery { int percent; int mV; int mA; int mAh; bool usb; };  // mA > 0 = charging; mAh = remaining
inline bool hw_battery_read(Battery &b) {
  uint8_t v[2];
  auto rd = [&](uint8_t reg) { return i2c_read(Wire1, 0x55, reg, false, v, 2) ? int16_t(v[0] | v[1] << 8) : INT16_MIN; };
  b.percent = rd(0x2C); b.mV = rd(0x08); b.mA = rd(0x0C) / GAUGE_SCALE; b.mAh = rd(0x10) / GAUGE_SCALE;
  b.usb = digitalRead(PIN_USB_PRESENT);
  return b.percent != INT16_MIN;
}

inline void hw_battery_log() {
  Battery b;
  if (hw_battery_read(b)) Serial.printf("[bat] %d%% %d mV %+d mA %d mAh usb=%d\n", b.percent, b.mV, b.mA, b.mAh, b.usb);
}

// ---------- all ----------
inline void hw_begin() {
  pinMode(PIN_POWER_HOLD, OUTPUT); digitalWrite(PIN_POWER_HOLD, HIGH);  // stay on when on battery
  pinMode(PIN_CHG_EN, OUTPUT); digitalWrite(PIN_CHG_EN, LOW);  // enable the battery charger (it stays off otherwise)
  pinMode(PIN_USB_PRESENT, INPUT);
  hw_buttons_begin();
  Wire1.begin(PIN_SENS_SDA, PIN_SENS_SCL, 100000);
  Serial.println(hw_rtc_to_clock() ? "[rtc] clock set from the RTC" : "[rtc] no valid time in the RTC");
  if (!hw_touch_begin()) Serial.println("[touch] GT911 not found on 0x5D/0x14");
  // Wire (touch) is used only by this task from here on; Wire1 (sensors) stays on the main loop.
  g_touchQ = xQueueCreate(32, sizeof(TouchEv));
  xTaskCreatePinnedToCore(touch_task, "touch", 3072, nullptr, 2, nullptr, 0);
}
