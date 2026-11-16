// HeySticky — TASK-04: Wi-Fi + phone setup page; shows the Web setup screen (04j).
#include <Seeed_GFX.h>
#include <qrcode.h>  // esp_qrcode, bundled with the ESP32 core
#include "hw.h"
#include "net.h"

Seeed_GFX display(Seeed_Product::reTerminal_Sticky);
constexpr int W = 480;

static int g_qrY;
static void drawQrModules(esp_qrcode_handle_t q) {
  const int n = esp_qrcode_get_size(q), s = 220 / n, x0 = (W - n * s) / 2;
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(q, x, y)) display.fillRect(x0 + x * s, g_qrY + y * s, s, s, TFT_BLACK);
}

// Web setup screen. Built-in font for now; JetBrains Mono screens come with TASK-06/10.
void drawWebSetup(bool ap) {
  display.fillScreen(TFT_WHITE);
  display.setTextColor(TFT_BLACK);
  display.setTextDatum(TL_DATUM);
  display.setTextSize(2);
  display.drawString("HeySticky", 16, 16);
  display.setTextSize(4);
  display.drawString("Web setup", 40, 72);
  display.drawFastHLine(40, 120, W - 80, TFT_BLACK);

  display.setTextDatum(TC_DATUM);
  display.setTextSize(3);
  display.drawString(ap ? "Join this Wi-Fi" : "Open this on your phone", W / 2, 150);
  display.fillRoundRect(40, 190, W - 80, 70, 12, TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(ap ? 3 : 4);
  display.drawString(ap ? AP_SSID : net_ip().c_str(), W / 2, ap ? 214 : 210);
  display.setTextColor(TFT_BLACK);

  // Hotspot: QR joins the Wi-Fi (the captive portal then opens the page). On Wi-Fi: QR opens the page.
  g_qrY = 290;
  String qr = ap ? String("WIFI:T:nopass;S:") + AP_SSID + ";;" : "http://" + net_ip() + "/";
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = drawQrModules;
  esp_qrcode_generate(&cfg, qr.c_str());

  display.setTextSize(2);
  if (ap) {
    display.drawString("Scan to join, or pick it in", W / 2, 540);
    display.drawString("your phone's Wi-Fi list.", W / 2, 566);
    display.drawString("Then open " + net_ip(), W / 2, 604);
  } else {
    display.drawString("Use the same Wi-Fi.", W / 2, 540);
  }
  display.update();
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) delay(10);
  hw_begin();
  hw_beep();
  if (!display.begin()) Serial.println(display.lastResult().message);
  display.setRotation(1);  // portrait, confirmed upright on the real panel
  const bool online = net_begin();
  drawWebSetup(!online);
}

void loop() {
  net_loop();
  static const char *names[] = {"", "TOP", "UP", "DOWN", "TOP_LONG"};
  if (HwButton b = hw_buttons_poll()) {
    Serial.printf("[btn] %s\n", names[b]);
    hw_beep(b == BTN_TOP_LONG ? 120 : 30);
  }
  delay(5);
}
