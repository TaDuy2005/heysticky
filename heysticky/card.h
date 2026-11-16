// microSD on the panel's SPI bus (Seeed_GFX2 owns the bus; never end()/begin() it) and /todo.md.
// Mount sequence follows Seeed's reTerminal_Sticky_SDcard_BW example.
#pragma once
#include <SD.h>
#include <bus/Bus_SPI.h>
#include <driver/gpio.h>
#include "pins.h"
#include "todo.h"

extern Seeed_GFX display;
inline bool g_sdOk = false;
inline bool g_todoUnsaved = false;  // a change that did not reach the card yet (card pulled)
inline std::vector<Task> g_tasks;

inline bool sd_mount() {
  SD.end();
  for (gpio_num_t p : {GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_8}) gpio_set_pull_mode(p, GPIO_PULLUP_ONLY);
  pinMode(PIN_SD_EN, OUTPUT);
  digitalWrite(PIN_SD_EN, HIGH);  // slot power
  pinMode(PIN_SD_DETECT, INPUT_PULLUP);
  delay(100);
  SPIClass *spi = static_cast<Bus_SPI &>(display.panel().driver().bus()).spiInstance();
  for (uint32_t hz : {4000000u, 1000000u}) {
    if (spi && SD.begin(PIN_SD_CS, *spi, hz)) {
      Serial.printf("[sd] mounted %lu MHz, %llu MB, detect=%d\n", hz / 1000000, SD.cardSize() >> 20, digitalRead(PIN_SD_DETECT));
      return g_sdOk = true;
    }
    SD.end();
  }
  Serial.printf("[sd] no card (detect=%d)\n", digitalRead(PIN_SD_DETECT));
  return g_sdOk = false;
}

inline bool sd_fail(const char *what) {
  Serial.printf("[sd] %s failed\n", what);
  return g_sdOk = false;
}

inline bool todo_load() {
  if (!g_sdOk && !sd_mount()) return false;
  File root = SD.open("/");
  if (!root) return sd_fail("open /");
  root.close();
  if (!SD.exists("/todo.md") && SD.exists("/todo.tmp")) SD.rename("/todo.tmp", "/todo.md");  // finish an interrupted save
  g_tasks.clear();
  File f = SD.open("/todo.md");
  if (!f) { Serial.println("[todo] no /todo.md yet"); return true; }  // empty list; the first save creates it
  while (f.available()) {
    String l = f.readStringUntil('\n');
    if (l.endsWith("\r")) l.remove(l.length() - 1);
    g_tasks.push_back(todo_parse(l));
  }
  f.close();
  Serial.printf("[todo] loaded %u lines\n", unsigned(g_tasks.size()));
  return true;
}

// Write /todo.tmp, then swap it in, so a reset mid-save never leaves a half-written /todo.md.
inline bool todo_save() {
  g_todoUnsaved = true;
  if (!g_sdOk) return false;
  File f = SD.open("/todo.tmp", FILE_WRITE);
  if (!f) return sd_fail("open /todo.tmp");
  size_t want = 0, put = 0;
  for (const Task &t : g_tasks) {
    const String l = todo_format(t) + "\n";
    want += l.length();
    put += f.print(l);
  }
  f.close();
  if (put != want) return sd_fail("write /todo.tmp");
  SD.remove("/todo.md");
  if (!SD.rename("/todo.tmp", "/todo.md")) return sd_fail("rename");
  g_todoUnsaved = false;
  Serial.printf("[todo] saved %u bytes\n", unsigned(want));
  return true;
}

// Retry from the no-card screen: keep an unsaved change rather than reloading over it.
inline bool todo_retry() {
  if (!sd_mount()) return false;
  return g_todoUnsaved ? todo_save() : todo_load();
}
