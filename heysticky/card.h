// microSD on the panel's SPI bus (Seeed_GFX2 owns the bus; never end()/begin() it), /todo.md and /notes/.
// Mount sequence follows Seeed's reTerminal_Sticky_SDcard_BW example.
#pragma once
#include <SD.h>
#include <algorithm>
#include <bus/Bus_SPI.h>
#include <driver/gpio.h>
#include "pins.h"
#include "todo.h"
#include "notes.h"

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

// ---------- /notes/NNN.md ----------
inline std::vector<NoteHead> g_notes;  // newest first
inline Note g_note;                    // the note being viewed / saved
inline bool g_noteUnsaved = false;     // g_note did not reach the card yet (card pulled)

inline String read_upto(File &f, unsigned cap) {
  String s;
  s.reserve(min<unsigned>(f.size(), cap));
  while (f.available() && s.length() < cap) s += char(f.read());
  return s;
}

inline bool notes_dir() {
  if (!g_sdOk && !sd_mount()) return false;
  if (!SD.exists("/notes") && !SD.mkdir("/notes")) return sd_fail("mkdir /notes");
  return true;
}

inline bool notes_scan() {
  if (!notes_dir()) return false;
  File dir = SD.open("/notes");
  if (!dir) return sd_fail("open /notes");
  g_notes.clear();
  std::vector<int> tmps;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const String name = f.name();
    const int num = note_file_num(name);
    if (num > 0 && !f.isDirectory()) {
      String head;  // title line + first lines of the body, enough for the preview
      while (f.available() && head.length() < 400) head += f.readStringUntil('\n') + "\n";
      const Note n = note_parse(head);
      g_notes.push_back({num, n.title, note_preview(n.body)});
    } else if (note_file_num(name, ".tmp") > 0) {
      tmps.push_back(note_file_num(name, ".tmp"));
    }
    f.close();
  }
  dir.close();
  bool again = false;
  for (int t : tmps) {  // finish an interrupted save, or drop a stale temp file
    if (SD.exists(note_path(t))) SD.remove(note_path(t, ".tmp"));
    else again |= SD.rename(note_path(t, ".tmp"), note_path(t));
  }
  if (again) return notes_scan();
  std::sort(g_notes.begin(), g_notes.end(), [](const NoteHead &a, const NoteHead &b) { return a.num > b.num; });
  Serial.printf("[notes] %u notes\n", unsigned(g_notes.size()));
  return true;
}

inline bool note_read(int num) {
  if (!g_sdOk) return false;
  File f = SD.open(note_path(num));
  if (!f) return sd_fail("open note");
  g_note = note_parse(read_upto(f, NOTE_BYTES));
  g_note.num = num;
  f.close();
  return true;
}

// Saves g_note (a new note takes the next number), via NNN.tmp + rename like todo_save().
inline bool note_save() {
  g_noteUnsaved = true;
  if (!notes_dir()) return false;
  if (!g_note.num) g_note.num = g_notes.empty() ? 1 : g_notes.front().num + 1;
  const String tmp = note_path(g_note.num, ".tmp"), path = note_path(g_note.num), s = note_format(g_note);
  File f = SD.open(tmp, FILE_WRITE);
  if (!f) return sd_fail("open note tmp");
  const size_t put = f.print(s);
  f.close();
  if (put != s.length()) return sd_fail("write note");
  SD.remove(path);
  if (!SD.rename(tmp, path)) return sd_fail("rename note");
  g_noteUnsaved = false;
  Serial.printf("[notes] saved %s, %u bytes\n", path.c_str(), unsigned(put));
  return notes_scan();
}

inline bool note_delete(int num) {
  if (!g_sdOk) return false;
  if (SD.exists(note_path(num)) && !SD.remove(note_path(num))) return sd_fail("remove note");
  return notes_scan();
}

inline bool notes_retry() {
  if (!sd_mount()) return false;
  return g_noteUnsaved ? note_save() : notes_scan();
}

// ---------- 04i SD Card / SD Files ----------
inline bool sd_inserted() { return digitalRead(PIN_SD_DETECT) == LOW; }  // card-detect switch pulls the pin low

struct SdFile { String path; uint32_t size; };
inline std::vector<SdFile> g_files;  // filled by sd_files_scan(), sorted by path
constexpr size_t SD_FILES_MAX = 500;  // ponytail: list cap, keeps RAM bounded on a full card

inline void sd_walk(File dir, int depth) {
  for (File f = dir.openNextFile(); f && g_files.size() < SD_FILES_MAX; f = dir.openNextFile()) {
    const String name = f.name();
    if (name.startsWith(".") || name == "System Volume Information") { f.close(); continue; }  // hidden / OS files
    if (f.isDirectory()) { if (depth < 4) sd_walk(f, depth + 1); }
    else g_files.push_back({f.path(), uint32_t(f.size())});
    f.close();
  }
}

inline bool sd_files_scan() {
  g_files.clear();
  if (!g_sdOk && !sd_mount()) return false;
  File root = SD.open("/");
  if (!root) return sd_fail("open /");
  sd_walk(root, 0);
  root.close();
  std::sort(g_files.begin(), g_files.end(), [](const SdFile &a, const SdFile &b) { return a.path < b.path; });
  return true;
}
