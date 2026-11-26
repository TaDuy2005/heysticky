// Phone setup page: To-do (06o / 06p), Notes (06q / 06r), Books (06s) and Report a bug (06u) API. The pages are in PAGE (net.h).
// Runs on the main loop (WebServer is polled from net_loop), so the card is never used by two tasks at once.
// A task is addressed by its line index plus the line as the phone saw it ("was"): if the Sticky changed the
// list meanwhile, the phone gets 409 and reloads instead of editing the wrong line.
#pragma once
#include "screens.h"

inline bool g_phoneTodo = false, g_phoneNotes = false, g_phoneBooks = false, g_phoneRdFont = false;  // changed from the phone: phone_loop() redraws

inline void phone_json(JsonDocument &d) {
  String s;
  serializeJson(d, s);
  g_web.sendHeader("Cache-Control", "no-store");
  g_web.send(200, "application/json", s);
}
// Every To-do / Notes call starts here: only on the address 04j chose (web_ok: 404 on the other network), then
// 503 = no card; the page says "Put a microSD card in the Sticky first."
inline bool phone_card() {
  if (!web_ok()) return false;
  if (g_sdOk || sd_mount()) return true;
  g_web.send(503, "text/plain", "no card");
  return false;
}
inline String one_line_arg(const char *k, unsigned max) {
  String s = g_web.arg(k);
  s.replace("\r", " ");
  s.replace("\n", " ");
  s.trim();
  return s.length() > max ? String() : s;
}

// ---------- To-do ----------
inline void api_counts() {  // 06 Setup rows: "To-do (n)" = open tasks, "Notes (n)"
  if (!web_ok()) return;
  JsonDocument d;
  d["todo"] = g_sdOk ? todo_left(g_tasks) : -1;
  d["notes"] = g_sdOk && notes_scan() ? int(g_notes.size()) : -1;
  phone_json(d);
}

inline void api_todo() {
  if (!phone_card()) return;
  JsonDocument d;
  const String now = now_key();
  d["now"] = now;
  JsonArray a = d["tasks"].to<JsonArray>();
  for (const int i : todo_order(g_tasks)) {
    const Task &t = g_tasks[i];
    JsonObject o = a.add<JsonObject>();
    o["i"] = i;
    o["was"] = todo_format(t);
    o["text"] = t.text;
    o["due"] = t.due;
    o["done"] = t.done;
    o["created"] = t.created;
    o["label"] = t.done ? String() : todo_label(t, now);
  }
  phone_json(d);
}

inline int phone_task(bool add) {  // index of the task the phone means; -1 = new (add) or gone (409 sent)
  const int i = g_web.arg("i").toInt();
  if (add && i < 0) return -1;
  if (i >= 0 && i < int(g_tasks.size()) && g_tasks[i].task && todo_format(g_tasks[i]) == g_web.arg("was")) return i;
  g_web.send(409, "text/plain", "changed");
  return -2;
}

inline void api_todo_set() {  // i (-1 = new), was, text, due ("" / YYYY-MM-DD / YYYY-MM-DD HH:MM), done
  if (!phone_card()) return;
  const String text = one_line_arg("text", 300), due = g_web.arg("due");
  if (!text.length() || (due.length() && !valid_due(due))) return g_web.send(400, "text/plain", "bad task");
  const int i = phone_task(true);
  if (i == -2) return;
  Task t = i >= 0 ? g_tasks[i] : Task();
  t.text = text;
  t.due = due;
  t.done = g_web.arg("done") == "1";
  if (i < 0) { t.created = now_key(); g_tasks.push_back(t); }
  else g_tasks[i] = t;
  if (!todo_save()) return g_web.send(503, "text/plain", "no card");
  g_phoneTodo = true;
  g_web.send(200, "text/plain", "ok");
}

inline void api_todo_del() {
  if (!phone_card()) return;
  const int i = phone_task(false);
  if (i < 0) return;
  g_tasks.erase(g_tasks.begin() + i);
  if (!todo_save()) return g_web.send(503, "text/plain", "no card");
  g_phoneTodo = true;
  g_web.send(200, "text/plain", "ok");
}

// ---------- Notes ----------
inline void api_notes() {
  if (!phone_card() || !notes_scan()) return;
  JsonDocument d;
  JsonArray a = d["notes"].to<JsonArray>();
  for (const NoteHead &n : g_notes) {
    JsonObject o = a.add<JsonObject>();
    o["num"] = n.num;
    o["title"] = n.title;
    o["preview"] = n.preview;
  }
  phone_json(d);
}

inline void api_note() {  // ?num=
  if (!phone_card()) return;
  const Note keep = g_note;  // the note the Sticky shows, if any
  if (!note_read(g_web.arg("num").toInt())) { g_note = keep; return g_web.send(404, "text/plain", "gone"); }
  JsonDocument d;
  d["num"] = g_note.num;
  d["title"] = g_note.title;
  d["body"] = g_note.body;
  d["created"] = g_note.created;
  g_note = keep;
  phone_json(d);
}

inline void api_note_set() {  // num (0 = new), title, body
  if (!phone_card()) return;
  const String title = one_line_arg("title", TITLE_MAX), body = g_web.arg("body");
  if (!title.length() && !body.length()) return g_web.send(400, "text/plain", "empty");
  if (title.length() + body.length() + 3 > NOTE_BYTES) return g_web.send(413, "text/plain", "too long");
  const int num = g_web.arg("num").toInt();
  const Note keep = g_note;
  if (num) {
    if (!note_read(num)) { g_note = keep; return g_web.send(404, "text/plain", "gone"); }  // keeps its created date and properties
  } else {
    notes_scan();  // the next note number comes from the list
    g_note = Note();
    g_note.created = now_key();
  }
  g_note.title = title;
  g_note.body = body;
  const bool ok = note_save();
  const int saved = g_note.num;
  g_note = keep;
  if (!ok) return g_web.send(503, "text/plain", "no card");
  g_phoneNotes = true;
  g_web.send(200, "text/plain", String(saved));
}

inline void api_note_del() {
  if (!phone_card()) return;
  if (!note_delete(g_web.arg("num").toInt())) return g_web.send(503, "text/plain", "no card");
  g_phoneNotes = true;
  g_web.send(200, "text/plain", "ok");
}

// ---------- Books (06s): /books/<file>; the Sticky reads them (Apps › Reader) ----------
constexpr size_t BOOK_MAX = 50u << 20;  // 50 MB a book
inline bool book_name_ok(const String &n) {
  if (!n.length() || n.length() > 120 || n.indexOf('/') >= 0 || n.indexOf('\\') >= 0 || !book_file(n)) return false;
  for (unsigned i = 0; i < n.length(); i++)
    if (uint8_t(n[i]) < 0x20) return false;
  return true;
}

inline void api_books() {
  if (!phone_card()) return;
  if (!books_scan()) return g_web.send(503, "text/plain", "no card");
  JsonDocument d;
  JsonArray a = d["books"].to<JsonArray>();
  for (const Book &b : g_books) {
    JsonObject o = a.add<JsonObject>();
    o["name"] = b.path.substring(strlen(BOOKS_DIR) + 1);
    o["title"] = b.title;
    o["author"] = b.author;
    o["size"] = b.size;
  }
  phone_json(d);
}

// One book per request (multipart): /api/book?name=Frankenstein.epub. Written as .part, renamed when complete.
inline File g_bookUp;
inline bool g_bookOk = false;
inline size_t g_bookN = 0;
inline void api_book_part() {
  HTTPUpload &u = g_web.upload();
  const String name = g_web.arg("name"), path = String(BOOKS_DIR) + "/" + name, part = String(READER_DIR) + "/upload.part";
  if (u.status == UPLOAD_FILE_START) {
    g_bookN = 0;
    g_bookOk = web_local() && (g_sdOk || sd_mount()) && book_name_ok(name);
    if (g_bookOk) { SD.mkdir(BOOKS_DIR); SD.mkdir(READER_DIR); g_bookUp = SD.open(part, FILE_WRITE); g_bookOk = bool(g_bookUp); }
  } else if (u.status == UPLOAD_FILE_WRITE) {
    g_bookN += u.currentSize;
    if (g_bookOk) g_bookOk = g_bookN <= BOOK_MAX && g_bookUp.write(u.buf, u.currentSize) == u.currentSize;
  } else {  // END or ABORTED
    if (g_bookUp) g_bookUp.close();
    g_bookOk = g_bookOk && u.status == UPLOAD_FILE_END && g_bookN > 0;
    if (g_bookOk) {
      rd::forget(path);  // a book of the same name is replaced
      SD.remove(path);
      g_bookOk = SD.rename(part, path);
    }
    if (!g_bookOk) SD.remove(part);
  }
}
inline void api_book_done() {
  if (!web_ok()) return;
  if (g_bookOk) g_phoneBooks = true;
  g_web.send(g_bookOk ? 200 : 400, "text/plain", g_bookOk ? "OK" : "Upload failed");
  g_bookOk = false;
}

inline void api_book_del() {
  if (!phone_card()) return;
  const String name = g_web.arg("name"), path = String(BOOKS_DIR) + "/" + name;
  if (!book_name_ok(name) || !SD.exists(path)) return g_web.send(404, "text/plain", "gone");
  rd::forget(path);
  if (!SD.remove(path)) return g_web.send(503, "text/plain", "no card");
  g_phoneBooks = true;
  g_web.send(200, "text/plain", "ok");
}

// ---------- Reader fonts (06s Fonts): /fonts/<Family>/<Family>_<size>.cpfont; the Sticky reads with them ----------
inline void api_rfonts() {
  if (!phone_card()) return;
  JsonDocument d;
  d["use"] = reader_font();
  JsonArray a = d["fonts"].to<JsonArray>();
  for (const ReaderFont &f : rfont_scan()) {
    JsonObject o = a.add<JsonObject>();
    o["name"] = f.name;
    JsonArray z = o["sizes"].to<JsonArray>();
    for (int n : f.sizes) z.add(n);
  }
  phone_json(d);
}

// One file per request (multipart): /api/rfont?name=Literata_12.cpfont. Checked (name, "CPFONT" header, version,
// size), written as .part and renamed into /fonts/<Family>/ when complete.
inline File g_rfUp;
inline bool g_rfOk = false;
inline size_t g_rfN = 0;
inline uint8_t g_rfHead[10];
inline void api_rfont_part() {
  HTTPUpload &u = g_web.upload();
  const String name = g_web.arg("name"), part = String(READER_DIR) + "/font.part";
  String fam;
  int size;
  if (u.status == UPLOAD_FILE_START) {
    g_rfN = 0;
    g_rfOk = web_local() && (g_sdOk || sd_mount()) && cpfont_name(name, fam, size);
    if (g_rfOk) { SD.mkdir(READER_DIR); g_rfUp = SD.open(part, FILE_WRITE); g_rfOk = bool(g_rfUp); }
  } else if (u.status == UPLOAD_FILE_WRITE) {
    for (size_t i = 0; i < u.currentSize && g_rfN + i < sizeof(g_rfHead); i++) g_rfHead[g_rfN + i] = u.buf[i];
    g_rfN += u.currentSize;
    if (g_rfOk) g_rfOk = g_rfN <= CPFONT_MAX && g_rfUp.write(u.buf, u.currentSize) == u.currentSize;
  } else {  // END or ABORTED
    if (g_rfUp) g_rfUp.close();
    g_rfOk = g_rfOk && u.status == UPLOAD_FILE_END && cpfont_header_ok(g_rfHead, min(g_rfN, sizeof(g_rfHead))) &&
             cpfont_name(name, fam, size);
    if (g_rfOk) {
      const String dir = "/fonts/" + fam, path = dir + "/" + name;
      SD.mkdir("/fonts");
      SD.mkdir(dir);
      SD.remove(path);  // the same file again replaces it
      g_rfOk = SD.rename(part, path);
    }
    if (!g_rfOk) SD.remove(part);
  }
}
inline void api_rfont_done() {
  if (!web_ok()) return;
  if (g_rfOk) g_phoneRdFont = true;
  const bool head = g_rfN >= sizeof(g_rfHead) && !cpfont_header_ok(g_rfHead, sizeof(g_rfHead));
  g_web.send(g_rfOk ? 200 : 400, "text/plain", g_rfOk ? "OK" : head ? "Not a CrossPoint font (.cpfont version 4)" : "Upload failed");
  g_rfOk = false;
}

inline void api_rfont_del() {  // the whole family: every .cpfont of it in /fonts and /.fonts
  if (!phone_card()) return;
  const String fam = g_web.arg("name");
  if (!cpfont_family_ok(fam)) return g_web.send(404, "text/plain", "gone");
  for (const char *root : {"/fonts/", "/.fonts/"}) {
    const String dir = root + fam;
    File d = SD.open(dir);
    if (!d) continue;
    std::vector<String> files;
    for (File e = d.openNextFile(); e; e = d.openNextFile()) {
      String ef;
      int sz;
      if (cpfont_name(e.name(), ef, sz) && ef == fam) files.push_back(dir + "/" + e.name());
      e.close();
    }
    d.close();
    for (const String &f : files) SD.remove(f);
    SD.rmdir(dir);  // only if nothing else is in it
  }
  if (reader_font() == fam) cfg_set("rd_font", "");
  g_phoneRdFont = true;
  g_web.send(200, "text/plain", "ok");
}

inline void api_rfont_use() {  // "" = Noto Sans (built-in)
  if (!phone_card()) return;
  const String fam = g_web.arg("name");
  if (fam.length() && !cpfont_family_ok(fam)) return g_web.send(400, "text/plain", "bad name");
  cfg_set("rd_font", fam);
  g_phoneRdFont = true;
  g_web.send(200, "text/plain", "ok");
}

// ---------- Report a bug ----------
// 06u Report on GitHub / by email: a new report ID and the link to open.
inline void api_report() {
  if (!web_ok()) return;
  const Report r{report_id(esp_random()), ""};  // not g_bug: the Sticky may be showing its own draft
  JsonDocument d;
  d["id"] = r.id;
  d["url"] = g_web.arg("via") == "mail" ? report_mail_url(r, "", g_vi) : report_gh_url(r, "", g_vi);
  phone_json(d);
}

inline void phone_begin() {
  g_web.on("/api/counts", HTTP_GET, api_counts);
  g_web.on("/api/todo", HTTP_GET, api_todo);
  g_web.on("/api/todo_set", HTTP_POST, api_todo_set);
  g_web.on("/api/todo_del", HTTP_POST, api_todo_del);
  g_web.on("/api/notes", HTTP_GET, api_notes);
  g_web.on("/api/note", HTTP_GET, api_note);
  g_web.on("/api/note_set", HTTP_POST, api_note_set);
  g_web.on("/api/note_del", HTTP_POST, api_note_del);
  g_web.on("/api/books", HTTP_GET, api_books);
  g_web.on("/api/book", HTTP_POST, api_book_done, api_book_part);
  g_web.on("/api/book_del", HTTP_POST, api_book_del);
  g_web.on("/api/rfonts", HTTP_GET, api_rfonts);
  g_web.on("/api/rfont", HTTP_POST, api_rfont_done, api_rfont_part);
  g_web.on("/api/rfont_del", HTTP_POST, api_rfont_del);
  g_web.on("/api/rfont_use", HTTP_POST, api_rfont_use);
  g_web.on("/api/report", HTTP_POST, api_report);
}

// The Sticky shows what the phone changed: a list (or Home's Today count) redraws; a task or note screen,
// whose item may be gone, goes back to its list.
inline void phone_loop() {
  if (g_phoneTodo) {
    g_phoneTodo = false;
    const Screen s = g_screen;
    if (s == SCR_TASK || s == SCR_DATE || s == SCR_TIME || s == SCR_DELTASK || s == SCR_DISCARD) show(SCR_TODO);
    else if (s == SCR_TODO || s == SCR_HOME) show(s);
  }
  if (g_phoneNotes) {
    g_phoneNotes = false;
    const Screen s = g_screen;
    if (s == SCR_NOTE || s == SCR_DELNOTE) { notes_scan(); show(SCR_NOTES); }
    else if (s == SCR_NOTES) { notes_scan(); show(s); }
  }
  if (g_phoneBooks) {  // the library is read again; an open book that was deleted closes
    g_phoneBooks = false;
    const Screen s = g_screen;
    if (s == SCR_LIBRARY || ((s == SCR_BOOK || s == SCR_CONTENTS) && !SD.exists(g_bookPath))) open_library();
  }
  if (g_phoneRdFont) {  // a font chosen, added or deleted on the phone: 07i / 07j redraw, an open book takes it
    g_phoneRdFont = false;
    const Screen s = g_screen;
    if (s == SCR_RDFONT) { g_rdFonts = rfont_scan(); show(s); }
    else if (s == SCR_RDSET) show(s);
    else if (s == SCR_BOOK && rd::font() != reader_font()) { g_rdSetFromBook = true; g_menu = false; reader_settings_back(); }
  }
}
