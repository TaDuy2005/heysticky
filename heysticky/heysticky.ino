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

// TASK-05 check screen: time, date, weather. Temporary; the real Home comes in TASK-06.
void drawClockCheck() {
  static const char *DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  static const char *MONTHS[] = {"January", "February", "March", "April", "May", "June", "July",
                                 "August", "September", "October", "November", "December"};
  display.fillScreen(TFT_WHITE);
  display.setTextColor(TFT_BLACK);
  display.setTextDatum(TL_DATUM);
  display.setTextSize(2);
  display.drawString("HeySticky", 16, 16);
  display.setTextDatum(TC_DATUM);
  display.setTextSize(3);
  display.drawString("Hello, " + cfg_get("name"), W / 2, 80);

  struct tm t;
  if (getLocalTime(&t, 0)) {
    char hm[6];
    strftime(hm, sizeof hm, "%H:%M", &t);
    display.setTextSize(12);
    display.drawString(hm, W / 2, 140);
    display.setTextSize(3);
    display.drawString(String(DAYS[t.tm_wday]) + ", " + t.tm_mday + " " + MONTHS[t.tm_mon], W / 2, 260);
  } else {
    display.setTextSize(4);
    display.drawString("--:--", W / 2, 170);
  }

  display.drawRoundRect(40, 320, W - 80, 200, 16, TFT_BLACK);
  display.setTextDatum(TL_DATUM);
  const char u = cfg_get("units", "C")[0];
  if (g_wx.ok) {
    display.setTextSize(3);
    display.drawString(wx_info(g_wx.code).label, 64, 344);
    display.setTextSize(6);
    display.drawString(String(lroundf(g_wx.temp)) + u, 64, 392);
    display.setTextSize(3);
    display.drawString("H " + String(lroundf(g_wx.hi)) + "  L " + String(lroundf(g_wx.lo)), 64, 456);
  } else {
    display.setTextSize(4);
    display.drawString("Offline", 64, 400);
  }
  display.setTextSize(2);
  display.drawString(cfg_get("city"), 64, 490);

  display.setTextDatum(TC_DATUM);
  display.drawString("Setup page: http://" + net_ip(), W / 2, 740);
  display.update();
}

bool g_online = false;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) delay(10);
  hw_begin();
  hw_beep();
  if (!display.begin()) Serial.println(display.lastResult().message);
  display.setRotation(1);  // portrait, confirmed upright on the real panel
  g_online = net_begin();
  if (!g_online) drawWebSetup(true);
}

void loop() {
  net_loop();
  if (g_online) {  // redraw when the minute or the weather changes
    static int lastMin = -1;
    static uint32_t lastWx = 1;
    struct tm t;
    const int m = getLocalTime(&t, 0) ? t.tm_hour * 60 + t.tm_min : -2;
    if (m != lastMin || g_wx.at != lastWx) {
      lastMin = m;
      lastWx = g_wx.at;
      drawClockCheck();
    }
  }
  static const char *names[] = {"", "TOP", "UP", "DOWN", "TOP_LONG"};
  if (HwButton b = hw_buttons_poll()) {
    Serial.printf("[btn] %s\n", names[b]);
    hw_beep(b == BTN_TOP_LONG ? 120 : 30);
  }
  delay(5);
}
