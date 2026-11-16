// HeySticky — TASK-01/02: HelloWorld on the reTerminal Sticky (portrait 480x800).
#include <Seeed_GFX.h>

Seeed_GFX display(Seeed_Product::reTerminal_Sticky);

void setup() {
  Serial.begin(115200);
  if (!display.begin()) {
    Serial.println(display.lastResult().message);
    return;
  }
  display.setRotation(1);  // portrait; ponytail: flip to 3 if upside down on the real panel
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
  Serial.printf("HeySticky HelloWorld: %dx%d\n", w, h);
}

void loop() { delay(1000); }
