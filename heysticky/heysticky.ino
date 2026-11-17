// HeySticky — personal assistant firmware for the reTerminal Sticky. See PLAN.md.
#include <Seeed_GFX.h>
#include "hw.h"
#include "net.h"

Seeed_GFX display(Seeed_Product::reTerminal_Sticky);

#include "screens.h"

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) delay(10);
  hw_begin();
  g_sound = cfg_get("sound") == "1";  // Settings › Sound, off by default
  hw_beep();
  if (!display.begin()) Serial.println(display.lastResult().message);
  display.setRotation(1);  // portrait, confirmed upright on the real panel
  display.fillScreen(TFT_WHITE);  // clear any ghost of the previous image before the first draw
  display.refresh();
  todo_load();  // after display.begin(): the card shares the panel's SPI bus
  const bool online = net_begin();
  if (online) net_loop();  // first weather fetch before the first Home draw
  show(online ? SCR_HOME : SCR_WEBSETUP);
}

void loop() {
  static uint32_t batLog = 0;  // battery state on serial every minute
  if (millis() - batLog > 60000 || !batLog) { batLog = millis() | 1; hw_battery_log(); }
  net_loop();
  screens_loop();
  delay(5);
}
