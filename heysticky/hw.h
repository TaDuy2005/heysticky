// HeySticky hardware: buttons, touch (GT911), buzzer, SHT40, BQ27220 battery gauge.
// Raw I2C, no extra libraries.
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "pins.h"

// ---------- buzzer ----------
inline bool g_sound = false;  // ponytail: muted until TASK-10's Settings › Sound toggle (Preferences "sound") sets it
inline void hw_beep(int ms = 40, int hz = 2700) { if (g_sound) tone(PIN_BUZZER, hz, ms); }

// ---------- buttons ----------
enum HwButton : uint8_t { BTN_NONE, BTN_TOP, BTN_UP, BTN_DOWN, BTN_TOP_LONG };
constexpr uint32_t LONG_PRESS_MS = 600;  // calibration knob: hold-to-talk threshold

struct BtnState { int pin; int idle; bool down; uint32_t since; bool longSent; };
inline BtnState g_btn[3] = {{PIN_BTN_TOP}, {PIN_BTN_UP}, {PIN_BTN_DOWN}};

inline void hw_buttons_begin() {
  for (auto &b : g_btn) {
    pinMode(b.pin, INPUT_PULLUP);
    delay(2);
    b.idle = digitalRead(b.pin);  // whatever level we boot with = released (no polarity guess)
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

inline void touch_task(void *) {
  bool down = false;
  int16_t lx = 0, ly = 0;
  uint32_t last = 0;
  auto push = [](int16_t x, int16_t y, bool d) { TouchEv e{x, y, d}; xQueueSend(g_touchQ, &e, 0); };
  for (;;) {
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
struct Battery { int percent; int mV; int mA; bool usb; bool chargePin; };
inline bool hw_battery_read(Battery &b) {
  uint8_t v[2];
  auto rd = [&](uint8_t reg) { return i2c_read(Wire1, 0x55, reg, false, v, 2) ? int16_t(v[0] | v[1] << 8) : INT16_MIN; };
  b.percent = rd(0x2C); b.mV = rd(0x08); b.mA = rd(0x0C);
  b.usb = digitalRead(PIN_USB_PRESENT); b.chargePin = digitalRead(PIN_CHG_STATE);
  return b.percent != INT16_MIN;
}

// ---------- all ----------
inline void hw_begin() {
  pinMode(PIN_POWER_HOLD, OUTPUT); digitalWrite(PIN_POWER_HOLD, HIGH);  // stay on when on battery
  pinMode(PIN_USB_PRESENT, INPUT); pinMode(PIN_CHG_STATE, INPUT);
  hw_buttons_begin();
  Wire1.begin(PIN_SENS_SDA, PIN_SENS_SCL, 100000);
  if (!hw_touch_begin()) Serial.println("[touch] GT911 not found on 0x5D/0x14");
  // Wire (touch) is used only by this task from here on; Wire1 (sensors) stays on the main loop.
  g_touchQ = xQueueCreate(32, sizeof(TouchEv));
  xTaskCreatePinnedToCore(touch_task, "touch", 3072, nullptr, 2, nullptr, 0);
  Serial.printf("[btn] idle levels top=%d up=%d down=%d\n", g_btn[0].idle, g_btn[1].idle, g_btn[2].idle);
}
