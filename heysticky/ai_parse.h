// AI requests and replies (PLAN_AI.md §4). No I/O here, so tools/ai_test.cpp checks it on the Mac.
// Speech-to-text: OpenAI-style multipart upload, reply {"text": "..."}.
// Sorting: OpenAI-style chat, the model answers one JSON object: todo / note / unsure.
#pragma once
#include <cctype>
#include <string>
#include <vector>
#include <ArduinoJson.h>
#include "todo.h"
#include "notes.h"

// Tested against DeepSeek V4 Flash on 2026-11-18 (V4.1 Flash 1017 via OpenRouter is the default since 2026-11-19) (EN, VI, ZH, mixed, ideas, mumbling, fake subtitles; see PLAN_AI.md AI-05).
// ponytail: a fluent fake ("明天上午9点，9号楼") still passes as a to-do; a loudness gate before upload would catch it.
constexpr const char *AI_PROMPT =
    "You sort one voice memo for a small e-paper to-do and notes device.\n"
    "Reply with ONE JSON object and nothing else:\n"
    "{\"type\":\"todo\",\"todos\":[{\"text\":\"…\",\"due\":\"YYYY-MM-DD HH:MM\"}]}  - things to do; one item per task; short "
    "imperative text starting with a capital letter; \"due\" only when a day or time was said for THAT task (never guess "
    "one); \"YYYY-MM-DD\" when no time; resolve against Now; \"by/before <day>\" means that day\n"
    "{\"type\":\"note\",\"title\":\"…\",\"body\":\"…\"}  - anything to keep that is not a task: ideas, thoughts, "
    "information (\"I have an idea…\" is a note even when it mentions doing something); follow the note instructions\n"
    "{\"type\":\"unsure\"}  - clear words, but you cannot tell whether they are a to-do or a note\n"
    "{\"type\":\"unclear\"}  - no clear meaning: mumbling, noise, filler words, a description of a sound\n"
    "Write every item, title and body in the Output language, translating anything said in another language.\n"
    "The transcript comes from speech-to-text, which turns mumbling or noise into fluent text in a random language "
    "(often a short Chinese, Japanese or Korean phrase, or subtitle lines like \"Thank you for watching\"). If the "
    "transcript does not read like something a person said to their to-do and notes device, reply unclear.";

constexpr unsigned AI_TASK_MAX = 200;  // bytes per to-do line text

inline const char *const DAY_NAMES[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// ---------- OpenRouter routing (06h1 / 06h2), sent only to an OpenRouter URL: other APIs may reject unknown fields ----------
// zdr: only hosts that keep no data; a model with none is refused, so the 06h test fails before it is saved.
// sort: "" = OpenRouter's balanced default, "throughput" = Nitro, "price" = Floor.
// order: "groq, deepinfra" = hosts tried first; fallback stays on, so a host that is down is not an error.
struct Route {
  bool on = false;
  String sort, order;
};
inline bool is_openrouter(const String &url) { return url.indexOf("openrouter.ai") >= 0; }
inline void route_json(JsonObject p, const Route &r) {
  p["zdr"] = true;
  if (r.sort.length()) p["sort"] = r.sort.c_str();
  std::string t;
  for (const char c : std::string(r.order.c_str()) + ",") {
    if (c != ',') {
      if (c != ' ') t += char(tolower(c));
    } else if (t.size()) {
      p["order"].add(t);
      t.clear();
    }
  }
}

// ---------- speech-to-text: multipart body = head + WAV + tail ----------
inline String mp_field(const String &boundary, const char *name, const String &value) {
  return "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + value + "\r\n";
}
inline String mp_head(const String &boundary, const String &model, const Route &r = Route()) {
  String prov;
  if (r.on) {
    JsonDocument p;
    route_json(p.to<JsonObject>(), r);
    std::string j;
    serializeJson(p, j);
    prov = mp_field(boundary, "provider", String(j.c_str()));
  }
  return mp_field(boundary, "model", model) + prov + "--" + boundary +
         "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"memo.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
}
inline String mp_tail(const String &boundary) { return "\r\n--" + boundary + "--\r\n"; }

inline String ai_text(const char *reply) {  // {"text": "..."} -> the transcript ("" if not that shape)
  JsonDocument d;
  if (deserializeJson(d, reply)) return "";
  String t = d["text"] | "";
  t.trim();
  return t;
}

// ---------- sorting ----------
// OpenRouter only: zero data retention + the 06h2 routing, and no thinking (~1 s instead of ~4 s, same sorting).
inline void route_chat(JsonDocument &d, const Route &r) {
  if (!r.on) return;
  route_json(d["provider"].to<JsonObject>(), r);
  d["reasoning"]["enabled"] = false;
}

// now = "YYYY-MM-DD HH:MM" local.
// vi: the device's system language; every item comes back in it, so the fonts can always draw it.
inline String ai_body(const String &model, const Route &r, bool vi, const String &now, const String &instructions, const String &transcript) {
  JsonDocument d;
  d["model"] = model.c_str();
  d["temperature"] = 0;
  d["response_format"]["type"] = "json_object";
  route_chat(d, r);
  JsonArray m = d["messages"].to<JsonArray>();
  JsonObject s = m.add<JsonObject>();
  s["role"] = "system";
  s["content"] = AI_PROMPT;
  std::string u = std::string("Now: ") + now.c_str();
  if (now.length() >= 10) u += std::string(" (") + DAY_NAMES[weekday(ymd_num(now))] + ")";
  u += std::string("\nOutput language: ") + (vi ? "Vietnamese" : "English");
  u += std::string("\nNote instructions: ") + (instructions.length() ? instructions.c_str() : "(none)");
  u += std::string("\nTranscript: ") + transcript.c_str();
  JsonObject us = m.add<JsonObject>();
  us["role"] = "user";
  us["content"] = u;
  std::string out;
  serializeJson(d, out);
  return String(out.c_str());
}

inline String ai_content(const char *reply) {  // chat reply -> choices[0].message.content
  JsonDocument d;
  if (deserializeJson(d, reply)) return "";
  return String(d["choices"][0]["message"]["content"] | "");
}

enum AiKind : uint8_t { AI_BAD, AI_TODO, AI_NOTE, AI_UNSURE, AI_UNCLEAR };
struct AiResult {
  AiKind kind = AI_BAD;
  std::vector<Task> todos;
  Note note;
};

inline String utf8_cut(const String &s, unsigned maxBytes) {  // at most maxBytes, never splitting a character
  if (s.length() <= maxBytes) return s;
  unsigned n = maxBytes;
  while (n && (uint8_t(s[n]) & 0xC0) == 0x80) n--;
  return s.substring(0, n);
}
inline String one_line(String s, unsigned maxBytes) {  // line breaks and tabs become spaces
  for (unsigned i = 0; i < s.length(); i++)
    if (s[i] == '\n' || s[i] == '\r' || s[i] == '\t') s[i] = ' ';
  s = utf8_cut(s, maxBytes);
  s.trim();
  return s;
}

// The model's answer; code fences or words around the JSON object are ignored.
inline AiResult ai_parse(const String &content) {
  AiResult r;
  const int a = content.indexOf('{'), b = content.lastIndexOf('}');
  if (a < 0 || b <= a) return r;
  JsonDocument d;
  if (deserializeJson(d, content.c_str() + a, size_t(b - a + 1))) return r;
  const String type = d["type"] | "";
  if (type == "todo") {
    for (JsonObject t : d["todos"].as<JsonArray>()) {
      Task k;
      k.text = one_line(t["text"] | "", AI_TASK_MAX);
      const String due = t["due"] | "";
      if (valid_due(due)) k.due = due;
      if (k.text.length()) r.todos.push_back(k);
    }
    if (!r.todos.empty()) r.kind = AI_TODO;
  } else if (type == "note") {
    r.note.title = one_line(d["title"] | "", TITLE_MAX);
    String body = utf8_cut(d["body"] | "", NOTE_BYTES - 3 - r.note.title.length());  // the note file limit
    body.trim();
    r.note.body = body;
    if (r.note.title.length() || r.note.body.length()) r.kind = AI_NOTE;
  } else if (type == "unsure") {
    r.kind = AI_UNSURE;
  } else if (type == "unclear") {
    r.kind = AI_UNCLEAR;
  }
  return r;
}
