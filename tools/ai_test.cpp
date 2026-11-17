// Host check for heysticky/ai_parse.h. Run from the repo root (ArduinoJson comes from the pinned Arduino profile):
//   c++ -std=c++17 -I heysticky -I "$(ls -d ~/Library/Arduino15/internal/ArduinoJson_7.4.3_*/ArduinoJson/src)" \
//       tools/ai_test.cpp -o /tmp/ai_test && /tmp/ai_test
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

// Just enough of Arduino's String for todo.h, notes.h and ai_parse.h.
struct String : std::string {
  String() {}
  String(const char *s) : std::string(s) {}
  String(const std::string &s) : std::string(s) {}
  String(int n) : std::string(std::to_string(n)) {}
  unsigned length() const { return unsigned(size()); }
  String substring(size_t a, size_t b = npos) const { return a >= size() ? "" : String(substr(a, b == npos ? npos : b - a)); }
  int indexOf(char c, size_t from = 0) const { auto p = find(c, from); return p == npos ? -1 : int(p); }
  int lastIndexOf(char c) const { auto p = rfind(c); return p == npos ? -1 : int(p); }
  int lastIndexOf(const char *s) const { auto p = rfind(s); return p == npos ? -1 : int(p); }
  bool startsWith(const String &s) const { return rfind(s, 0) == 0; }
  bool endsWith(const char *s) const { std::string t(s); return size() >= t.size() && compare(size() - t.size(), t.size(), t) == 0; }
  long toInt() const { return atol(c_str()); }
  void trim() {
    const auto a = find_first_not_of(" \t\r\n"), b = find_last_not_of(" \t\r\n");
    *this = a == npos ? String("") : String(substr(a, b - a + 1));
  }
};
inline String operator+(const String &a, const String &b) { return String(static_cast<const std::string &>(a) + static_cast<const std::string &>(b)); }
inline String operator+(const String &a, const char *b) { return String(static_cast<const std::string &>(a) + b); }
inline String operator+(const char *a, const String &b) { return String(a + static_cast<const std::string &>(b)); }

#include "ai_parse.h"

int main() {
  // multipart: model field, then the file; the tail closes the boundary
  const String h = mp_head("B1", "openai/whisper-large-v3"), t = mp_tail("B1");
  assert(h == "--B1\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nopenai/whisper-large-v3\r\n--B1\r\n"
              "Content-Disposition: form-data; name=\"file\"; filename=\"memo.wav\"\r\nContent-Type: audio/wav\r\n\r\n");
  assert(t == "\r\n--B1--\r\n");

  // speech-to-text reply (OpenRouter's, recorded 2026-11-18)
  assert(ai_text("{\"text\":\" Hello, hello, how are you?  Do you hear me?\",\"usage\":{\"seconds\":5.92}}") ==
         "Hello, hello, how are you?  Do you hear me?");
  assert(ai_text("{\"error\":{\"message\":\"bad\"}}") == "" && ai_text("not json") == "");

  // chat request: thinking switch only when asked; weekday from the date; instructions and transcript in the user turn
  JsonDocument d;
  assert(!deserializeJson(d, ai_body("deepseek-v4-flash", true, false, "2026-11-18 21:30", "", "Mua sữa").c_str()));
  assert(String(d["model"] | "") == "deepseek-v4-flash" && String(d["thinking"]["type"] | "") == "disabled");
  assert(String(d["response_format"]["type"] | "") == "json_object" && d["temperature"] == 0);
  assert(String(d["messages"][0]["role"] | "") == "system" &&
         String(d["messages"][1]["content"] | "") ==
             "Now: 2026-11-18 21:30 (Wednesday)\nOutput language: English\nNote instructions: (none)\nTranscript: Mua sữa");
  assert(!deserializeJson(d, ai_body("~deepseek/deepseek-flash-latest", false, true, "", "# Title", "x").c_str()));
  assert(d["thinking"].isNull() &&
         String(d["messages"][1]["content"] | "") == "Now: \nOutput language: Vietnamese\nNote instructions: # Title\nTranscript: x");

  // chat reply -> content
  assert(ai_content("{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"{\\\"type\\\":\\\"unsure\\\"}\"}}]}") == "{\"type\":\"unsure\"}");
  assert(ai_content("{\"error\":\"x\"}") == "");

  // to-dos (DeepSeek's answer, recorded 2026-11-18); a bad date is dropped, an empty item skipped
  AiResult r = ai_parse("{\"type\":\"todo\",\"todos\":[{\"text\":\"Gọi nha sĩ\",\"due\":\"2026-11-19 09:00\"},{\"text\":\"Mua sữa\"},"
                        "{\"text\":\"Gửi report\\ncho team\",\"due\":\"2026-11-20\"},{\"text\":\"Bad\",\"due\":\"Friday\"},{\"text\":\"  \"}]}");
  assert(r.kind == AI_TODO && r.todos.size() == 4);
  assert(r.todos[0].text == "Gọi nha sĩ" && r.todos[0].due == "2026-11-19 09:00");
  assert(r.todos[1].due == "" && r.todos[2].text == "Gửi report cho team" && r.todos[2].due == "2026-11-20" && r.todos[3].due == "");
  assert(todo_format(r.todos[0]) == "- [ ] Gọi nha sĩ @due(2026-11-19 09:00)");

  // note, wrapped in a code fence and words; title on one line
  r = ai_parse("Sure:\n```json\n{\"type\":\"note\",\"title\":\"Ý tưởng\\napp\",\"body\":\"- Habit grid 7 nhân 5\\n- Tô đen khi xong\"}\n```");
  assert(r.kind == AI_NOTE && r.note.title == "Ý tưởng app" && r.note.body == "- Habit grid 7 nhân 5\n- Tô đen khi xong");
  assert(note_format(r.note) == "# Ý tưởng app\n- Habit grid 7 nhân 5\n- Tô đen khi xong");

  // unsure, broken, empty, unknown type, todo with no items
  assert(ai_parse("{\"type\":\"unsure\"}").kind == AI_UNSURE && ai_parse("{\"type\":\"unclear\"}").kind == AI_UNCLEAR);
  assert(ai_parse("{\"type\":\"todo\",").kind == AI_BAD && ai_parse("").kind == AI_BAD && ai_parse("{\"type\":\"poem\"}").kind == AI_BAD);
  assert(ai_parse("{\"type\":\"todo\",\"todos\":[]}").kind == AI_BAD && ai_parse("{\"type\":\"note\"}").kind == AI_BAD);

  // cuts never split a UTF-8 character
  assert(utf8_cut("ệệ", 4) == "ệ" && utf8_cut("abc", 5) == "abc" && one_line("a\r\nb", 10) == "a  b");
  const String long_text(std::string(300, 'x'));
  r = ai_parse(("{\"type\":\"todo\",\"todos\":[{\"text\":\"" + long_text + "\"}]}").c_str());
  assert(r.todos[0].text.length() == AI_TASK_MAX);

  puts("ai_parse OK");
}
