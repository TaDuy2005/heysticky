// HeySticky — personal assistant firmware for the reTerminal Sticky. See PLAN.md.
#include <Seeed_GFX.h>
#include "hw.h"
#include "net.h"

Seeed_GFX display(Seeed_Product::reTerminal_Sticky);

#include "screens.h"

extern "C" bool verifyRollbackLater() { return true; }  // a new OTA image confirms itself at the end of setup()

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) delay(10);
  hw_begin();
  g_sound = cfg_get("sound") == "1";  // Settings › Sound, off by default
  g_vi = cfg_get("lang") == "vi";     // Settings › Language & Font › System Language
  hw_beep();
  if (!display.begin()) Serial.println(display.lastResult().message);
  display.setRotation(1);  // portrait, confirmed upright on the real panel
  display.fillScreen(TFT_WHITE);  // clear any ghost of the previous image before the first draw
  display.refresh();
  todo_load();  // after display.begin(): the card shares the panel's SPI bus
  font_apply(cfg_get("font"));  // user font from the card; built-in if it is missing
  show(net_begin() ? SCR_HOME : SCR_WEBSETUP);  // Wi-Fi joins in the background; no saved network -> hotspot + 04j
  ota_mark_good();  // display, touch and SD came up: keep this image (no rollback)
}

void loop() {
  static uint32_t batLog = 0;  // battery state on serial every minute
  if (millis() - batLog > 60000 || !batLog) { batLog = millis() | 1; hw_battery_log(); }
  net_loop();
  if (ota_loop() && g_screen == SCR_UPDATE) g_redraw = true;
  screens_loop();
  delay(5);
}
