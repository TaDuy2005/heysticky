// HeySticky — TASK-03: HelloWorld + hardware check over serial (115200).
#include <Seeed_GFX.h>
#include "hw.h"

Seeed_GFX display(Seeed_Product::reTerminal_Sticky);

void drawHello() {
  display.setRotation(1);  // portrait, confirmed upright on the real panel
  display.fillScreen(TFT_WHITE);
  display.refresh();
  delay(500);
  display.fillScreen(TFT_WHITE);
  const int16_t w = display.width(), h = display.height();
  display.setTextColor(TFT_BLACK);
  display.setTextSize(1);
  display.drawString("HeySticky", 16, 16);
  display.drawFastHLine(16, 56, w - 32, TFT_BLACK);
  display.setTextSize(5);
  display.drawString("Hello", 40, 300);
  display.drawString("World", 40, 360);
  display.setTextSize(2);
  display.drawString(String(w) + " x " + String(h), 40, 460);
  display.update();
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) delay(10);  // let USB serial attach so boot lines aren't lost
  hw_begin();
  hw_beep();
  if (display.begin()) drawHello();
  else Serial.println(display.lastResult().message);
}

void loop() {
  static const char *names[] = {"", "TOP", "UP", "DOWN", "TOP_LONG"};
  if (HwButton b = hw_buttons_poll()) {
    Serial.printf("[btn] %s\n", names[b]);
    hw_beep(b == BTN_TOP_LONG ? 120 : 30);
  }

  static uint32_t lastTouch = 0;
  int16_t x, y;
  if (hw_touch_get(x, y) && millis() - lastTouch > 300) {  // one line per tap
    lastTouch = millis();
    Serial.printf("[touch] screen x=%d y=%d\n", x, y);
  }

  static uint32_t lastSense = 0;
  if (millis() - lastSense > 5000) {
    lastSense = millis();
    float t, rh;
    Battery bat;
    if (hw_sht40_read(t, rh)) Serial.printf("[sht40] %.1f C  %.0f %%RH\n", t, rh);
    else Serial.println("[sht40] no reply");
    if (hw_battery_read(bat)) Serial.printf("[bat] %d %%  %d mV  %d mA  usb=%d chg_pin=%d\n", bat.percent, bat.mV, bat.mA, bat.usb, bat.chargePin);
    else Serial.println("[bat] no reply");
  }
  delay(10);
}
