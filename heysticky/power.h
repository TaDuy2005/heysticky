// TASK-11 Sleep after (04l): idle -> Home with the moon (01b/01d), then light sleep.
// Asleep: Wi-Fi and touch off, CPU at 80 MHz; a timer wakes it each minute for the clock and every
// 30 min Wi-Fi comes on for weather + NTP. Only the top button wakes it fully, to Home.
#pragma once
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <esp_sntp.h>
#include "screens.h"

constexpr uint32_t SYNC_EVERY_MS = 30UL * 60 * 1000, SYNC_TIMEOUT_MS = 40000;
inline String g_sleepReport;  // last sleep's battery use, repeated in the minute log (read it after plugging USB back in)

inline bool may_sleep() {
  if (!g_sleepMin || g_webOn || g_ota == OTA_CHECKING || g_ota == OTA_INSTALLING) return false;
  switch (g_screen) {  // an editor is open: typed work would be lost
    case SCR_TASK: case SCR_DATE: case SCR_TIME: case SCR_NOTEED: case SCR_DISCARD: return false;
    default: return millis() - g_lastInput >= g_sleepMin * 60000UL;
  }
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
  if (!g_wifi.count) return;
  const uint32_t t0 = millis(), wxAt = g_wx.at;
  WiFi.mode(WIFI_STA);
  g_retryAt = 0;
  g_wx_next = 0;
  bool asked = false, synced = false;
  while (millis() - t0 < SYNC_TIMEOUT_MS && !(g_wx.at != wxAt && synced) && !top_pressed()) {
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
  wifi_off();
}

inline uint64_t us_to_next_minute() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  return (60 - tv.tv_sec % 60) * 1000000ULL - tv.tv_usec + 100000;  // just after the minute turns
}

inline void power_sleep() {
  Battery b0;
  hw_battery_read(b0);
  const uint32_t t0 = millis();
  Serial.printf("[sleep] in after %d min idle\n", g_sleepMin);
  g_asleep = true;
  show(SCR_HOME);  // 01b / 01d
  touch_power(false);
  wifi_off();
  setCpuFrequencyMhz(80);
  // Keep these pins as they are while asleep (power hold above all: low = the Sticky turns off on battery).
  for (const int p : {PIN_POWER_HOLD, PIN_CHG_EN, PIN_TP_EN, PIN_BTN_TOP}) gpio_sleep_sel_dis(gpio_num_t(p));
  gpio_wakeup_enable(gpio_num_t(PIN_BTN_TOP), g_btn[0].idle ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  uint32_t syncAt = g_wx.at && millis() - g_wx.at < SYNC_EVERY_MS ? g_wx.at + SYNC_EVERY_MS : millis();
  while (!top_pressed()) {
    if (int32_t(millis() - syncAt) >= 0) { sleep_sync(); syncAt = millis() + SYNC_EVERY_MS; }
    home_tick();  // clock each minute; weather, battery and indoor when they changed
    if (top_pressed()) break;
    Serial.printf("[sleep] tick %s\n", g_shown.clock.c_str());
    Serial.flush();
    esp_sleep_enable_timer_wakeup(us_to_next_minute());
    esp_light_sleep_start();
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
  show(SCR_HOME);
}

inline void power_loop() {
  if (may_sleep()) power_sleep();
}
