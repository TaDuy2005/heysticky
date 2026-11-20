// TASK-11 Power. Sleep after (04l): idle -> Home with the moon (01b/01d), then light sleep.
// Asleep: Wi-Fi and touch off, CPU at 80 MHz; a timer wakes it each minute for the clock and every
// hour Wi-Fi comes on for weather + NTP; each wake re-reads the RTC (light sleep's clock drifts). Only the top button wakes it fully, to Home.
#pragma once
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <esp_sntp.h>
#include <driver/rtc_io.h>
#include "screens.h"

constexpr uint32_t SYNC_EVERY_MS = 60UL * 60 * 1000, SYNC_TIMEOUT_MS = 40000;
inline String g_sleepReport;  // last sleep's battery use, repeated in the minute log (read it after plugging USB back in)

constexpr uint32_t WEB_IDLE_MS = 5UL * 60 * 1000;  // Web setup turns off after this long with no phone page request and no touch

inline bool may_sleep() {
  if (!g_sleepMin || g_webOn || g_ota == OTA_CHECKING || g_ota == OTA_INSTALLING) return false;
  if (g_screen == SCR_LISTEN || g_screen == SCR_WORKING) return false;  // they end by themselves
  return millis() - g_lastInput >= g_sleepMin * 60000UL;
}

// Editors sleep on their own screen (no Home, no moon): light sleep keeps the typing, the top button comes back to it.
inline bool editor_screen() {
  return g_screen == SCR_TASK || g_screen == SCR_DATE || g_screen == SCR_TIME || g_screen == SCR_NOTEED || g_screen == SCR_DISCARD;
}

inline void touch_power(bool on) {
  g_touchOff = !on;
  for (uint32_t t0 = millis(); g_touchParked == on && millis() - t0 < 1000;) delay(10);
  xQueueReset(g_touchQ);
}

inline void wifi_off() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  g_ns = NS_IDLE;
  g_wasOn = false;
}

inline bool top_pressed() { return digitalRead(PIN_BTN_TOP) != g_btn[0].idle; }

// Wi-Fi on long enough for weather and one NTP reply (the sleep clock drifts), then off again.
inline void sleep_sync() {
  if (!g_wifiOn || !g_wifi.count) return;
  const uint32_t t0 = millis(), wxAt = g_wx.at;
  WiFi.mode(WIFI_STA);
  g_retryAt = 0;
  g_wx_next = 0;
  bool asked = false, synced = false;
  // Done with the weather: its Date header has set the time when NTP is blocked; NTP gets 3 s more if it is slow.
  uint32_t wxDone = 0;
  while (millis() - t0 < SYNC_TIMEOUT_MS && !(wxDone && (synced || millis() - wxDone > 3000)) && !top_pressed()) {
    if (!wxDone && g_wx.at != wxAt) wxDone = millis();
    net_loop();
    if (!asked && WiFi.status() == WL_CONNECTED && g_utc_off != LONG_MIN) {
      sntp_get_sync_status();  // clear an old "completed"
      configTime(g_utc_off, 0, "pool.ntp.org", "time.google.com");  // restarts SNTP: asks now
      asked = true;
    }
    if (asked && !synced) synced = sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
    delay(50);
  }
  Serial.printf("[sleep] sync %s, weather %s, ntp %s, %lu ms\n", WiFi.status() == WL_CONNECTED ? "online" : "offline",
                g_wx.at != wxAt ? "ok" : "--", synced ? "ok" : "--", millis() - t0);
  net_store_time();
  wifi_off();
}

inline uint64_t us_to_next_minute() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  return (60 - tv.tv_sec % 60) * 1000000ULL - tv.tv_usec + 100000;  // just after the minute turns
}

// ---------- shutdown (01e Turned off / 01f Battery empty), like Seeed's: hold UP + DOWN, or battery empty ----------
constexpr uint32_t OFF_HOLD_MS = 3000;
constexpr int BATTERY_EMPTY_PCT = 3;  // calibration knob: gauge % at which it turns itself off (on battery only)

constexpr uint32_t ON_HOLD_MS = 2000;  // calibration knob: top-button hold (from boot) that turns it on
constexpr int OFF_LOW_PINS[] = {PIN_CHG_EN, PIN_TP_EN, PIN_TP_INT, PIN_TP_RST, PIN_TP_SDA, PIN_TP_SCL};

// Off, no screen change. On battery the power goes here. On USB it can't, so deep sleep instead: touch pins and
// CHG_EN (low = keeps charging) held low, woken by the top button, then power_on_check() wants the 2 s hold.
[[noreturn]] inline void power_cut() {
  Serial.flush();
  pinMode(PIN_POWER_HOLD, OUTPUT);
  digitalWrite(PIN_POWER_HOLD, LOW);
  delay(1000);
  Serial.println("[power] on USB: deep sleep until the top button");
  Serial.flush();
  for (const int p : OFF_LOW_PINS) { pinMode(p, OUTPUT); digitalWrite(p, LOW); gpio_hold_en(gpio_num_t(p)); }
  gpio_deep_sleep_hold_en();
  const gpio_num_t top = gpio_num_t(PIN_BTN_TOP);
  rtc_gpio_pullup_en(top);
  rtc_gpio_pulldown_dis(top);
  esp_sleep_enable_ext0_wakeup(top, 0);
  esp_deep_sleep_start();
}

// First thing in setup(). Started by the button (on battery, or woken from USB off): only a 2 s top hold
// turns it on; a tap, a touch or another button goes straight back off. USB plug-in, reset, flashing: on.
inline void power_on_check() {
  for (const int p : OFF_LOW_PINS) gpio_hold_dis(gpio_num_t(p));
  pinMode(PIN_POWER_HOLD, OUTPUT);
  digitalWrite(PIN_POWER_HOLD, HIGH);
  rtc_gpio_deinit(gpio_num_t(PIN_BTN_TOP));
  pinMode(PIN_BTN_TOP, INPUT_PULLUP);
  pinMode(PIN_USB_PRESENT, INPUT);
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0 &&
      !(esp_reset_reason() == ESP_RST_POWERON && !digitalRead(PIN_USB_PRESENT))) return;
  while (millis() < ON_HOLD_MS) { if (digitalRead(PIN_BTN_TOP)) power_cut(); delay(10); }
  g_vol = cfg_get("vol", "2").toInt();  // setup() has not loaded it yet
  hw_tune(TUNE_ON);
  while (!digitalRead(PIN_BTN_TOP) && millis() < ON_HOLD_MS + 3000) delay(10);  // this hold only turns it on
}

// The message stays on the e-paper after the power is cut.
[[noreturn]] inline void power_off(bool empty) {
  Serial.printf("[power] off: %s\n", empty ? "battery empty" : "UP+DOWN held");
  hw_tune(TUNE_OFF);
  display.fillScreen(PAPER);
  ui_icon(empty ? ICON_BAT_EMPTY96 : ICON_POWER96, 192, 232);
  ui_text(F_X40, empty ? TR("Battery empty", "Hết pin") : TR("Turned off", "Đã tắt"), W / 2, 360, 52, AL_CENTER);
  ui_text(F_R30, empty ? TR("Charge the Sticky", "Sạc Sticky") : TR("Hold the top button", "Giữ nút trên"), W / 2, 428, 40, AL_CENTER);
  ui_text(F_R30, empty ? TR("to turn it on.", "để bật lại.") : TR("to turn on.", "để bật."), W / 2, 468, 40, AL_CENTER);
  ui_full();
  touch_power(false);
  wifi_off();
  power_cut();
}

// Call about once a minute: two low readings in a row, on battery.
inline bool battery_empty() {
  static int low = 0;
  Battery b;
  if (!hw_battery_read(b) || b.usb || b.percent > BATTERY_EMPTY_PCT) { low = 0; return false; }
  Serial.printf("[power] battery low %d%% %d mV (%d)\n", b.percent, b.mV, low + 1);
  return ++low >= 2;
}

inline void power_sleep() {
  Battery b0;
  hw_battery_read(b0);
  const uint32_t t0 = millis();
  Serial.printf("[sleep] in after %d min idle\n", g_sleepMin);
  const bool stay = editor_screen();
  g_asleep = true;
  if (!stay) show(SCR_HOME);  // 01b / 01d
  touch_power(false);
  wifi_off();
  setCpuFrequencyMhz(80);
  // Keep these pins as they are while asleep (power hold above all: low = the Sticky turns off on battery).
  for (const int p : {PIN_POWER_HOLD, PIN_CHG_EN, PIN_TP_EN, PIN_BTN_TOP}) gpio_sleep_sel_dis(gpio_num_t(p));
  gpio_wakeup_enable(gpio_num_t(PIN_BTN_TOP), g_btn[0].idle ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  uint32_t syncAt = g_wx.at && millis() - g_wx.at < SYNC_EVERY_MS ? g_wx.at + SYNC_EVERY_MS : millis();
  while (!top_pressed()) {
    if (!stay && int32_t(millis() - syncAt) >= 0) { sleep_sync(); syncAt = millis() + SYNC_EVERY_MS; }
    if (!stay) home_tick();  // clock each minute; weather, battery and indoor when they changed; an editor only checks the battery
    if (top_pressed()) break;
    if (battery_empty()) power_off(true);
    Serial.printf("[sleep] tick %s\n", g_shown.clock.c_str());
    Serial.flush();
    esp_sleep_enable_timer_wakeup(us_to_next_minute());
    esp_light_sleep_start();
    hw_rtc_to_clock();
  }
  gpio_wakeup_disable(gpio_num_t(PIN_BTN_TOP));
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  setCpuFrequencyMhz(240);
  for (const uint32_t t = millis(); top_pressed() && millis() - t < 3000;) delay(10);  // this press only wakes
  g_btn[0].down = false;
  Battery b1;
  hw_battery_read(b1);
  const float h = (millis() - t0) / 3600000.0f;
  g_sleepReport = b0.usb || b1.usb ? String("[sleep] last: ") + String(h, 2) + " h on USB, no battery figure"
                                   : String("[sleep] last: ") + String(h, 2) + " h, " + (b0.mAh - b1.mAh) + " mAh, " +
                                         String(h > 0 ? (b0.mAh - b1.mAh) / h : 0, 1) + " mA avg";
  Serial.println(g_sleepReport);
  WiFi.mode(WIFI_STA);
  g_retryAt = 0;  // background join right away
  touch_power(true);
  g_asleep = false;
  g_lastInput = millis();
  if (!stay) show(SCR_HOME);
}

inline void power_loop() {
  static uint32_t both = 0, batAt = 0;  // UP + DOWN held since (0 = not held); last battery check
  const bool held = digitalRead(PIN_BTN_UP) != g_btn[1].idle && digitalRead(PIN_BTN_DOWN) != g_btn[2].idle;
  if (!held) both = 0;
  else if (!both) both = millis() | 1;
  else if (millis() - both >= OFF_HOLD_MS) power_off(false);
  if (millis() - batAt >= 60000 && g_ota != OTA_INSTALLING) { batAt = millis(); if (battery_empty()) power_off(true); }
  if (g_webOn && millis() - g_webSeen >= WEB_IDLE_MS && millis() - g_lastInput >= WEB_IDLE_MS) {  // phone gone: hotspot off
    Serial.println("[net] no phone for 5 min");
    net_hotspot(false);
    if (g_screen == SCR_WEBSETUP) g_redraw = true;
  }
  if (may_sleep()) power_sleep();
}
