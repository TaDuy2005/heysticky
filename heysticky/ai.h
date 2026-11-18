// AI calls on the Sticky (PLAN_AI.md): speech-to-text, then sorting into to-dos or a note.
// Both are OpenAI-style POSTs over HTTPS, certificates checked with the core's root CA bundle.
// They run in their own task (ai_start) so 05a can animate; settings and the card are read before it starts,
// so the task touches neither flash nor the SD/panel SPI bus.
#pragma once
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include "net.h"
#include "mic.h"
#include "ai_parse.h"

constexpr uint32_t AI_TIMEOUT_MS = 30000;  // calibration knob: a 60 s clip takes a few seconds to upload and transcribe

enum AiFail : uint8_t { AF_NONE, AF_SHORT, AF_NONET, AF_NOKEY, AF_STT, AF_EMPTY, AF_AI, AF_REPLY };

struct AiCfg {
  String sttUrl, sttModel, sttKey, url, model, key, instr;
  bool plain = false;  // 06h Plain text: a note keeps the exact transcript, the AI only picks to-do or note
};

inline AiCfg ai_cfg() {  // an empty AI URL or key means the speech-to-text one
  AiCfg c{cfg_get("stt_url", AI_URL_DEF), cfg_get("stt_model", STT_MODEL_DEF), cfg_get("stt_key"),
          cfg_get("ai_url"), cfg_get("ai_model", AI_MODEL_DEF), cfg_get("ai_key"), ""};
  if (c.url.isEmpty()) c.url = c.sttUrl;
  if (c.key.isEmpty()) c.key = c.sttKey;
  c.plain = cfg_get("ai_plain") == "1";
  if (g_sdOk && !c.plain && cfg_get("ai_instr", "1") == "1") {  // 06h Note instructions toggle
    File f = SD.open(NOTE_INSTR_PATH);
    if (f) { c.instr = read_upto(f, NOTE_INSTR_MAX * 4); f.close(); }
  }
  return c;
}

// POST body (n bytes) to url; reply holds the answer. Returns the HTTP status, or < 0 when there was no answer.
inline int ai_post(const String &url, const String &key, const String &ctype, const uint8_t *body, size_t n, String &reply) {
  NetworkClientSecure tls;
  NetworkClient plain;  // http:// for a self-hosted server on the home network
  tls.useBuiltinCACertBundle();
  HTTPClient http;
  http.setTimeout(AI_TIMEOUT_MS);
  http.setConnectTimeout(10000);
  if (!http.begin(url.startsWith("https://") ? (NetworkClient &)tls : plain, url)) return -1;
  http.addHeader("Authorization", "Bearer " + key);
  http.addHeader("Content-Type", ctype);
  const uint32_t t0 = millis();
  const int code = http.sendRequest("POST", (uint8_t *)body, n);
  reply = code > 0 ? http.getString() : String();
  http.end();
  Serial.printf("[ai] POST %s: %d in %lu ms, %u bytes up, %u down\n", url.c_str(), code, millis() - t0, unsigned(n), reply.length());
  if (code != 200) Serial.printf("[ai] reply: %.300s\n", reply.c_str());
  return code;
}

// The take in g_wav (wavSize bytes) -> text. The multipart head and tail are written into the room mic.h keeps around it.
inline int ai_transcribe(const AiCfg &c, size_t wavSize, String &text) {
  const String b = "HeySticky" + String(esp_random(), HEX);
  const String head = mp_head(b, c.sttModel), tail = mp_tail(b);
  if (head.length() > MIC_PRE || tail.length() > MIC_POST) return -2;
  uint8_t *start = g_wav - head.length();
  memcpy(start, head.c_str(), head.length());
  memcpy(g_wav + wavSize, tail.c_str(), tail.length());
  String reply;
  const int code = ai_post(c.sttUrl + "/audio/transcriptions", c.sttKey, "multipart/form-data; boundary=" + b, start,
                           head.length() + wavSize + tail.length(), reply);
  if (code == 200) text = ai_text(reply.c_str());
  return code;
}

// Transcript -> to-dos / note / unsure.
inline int ai_sort(const AiCfg &c, const String &transcript, const String &now, AiResult &r) {
  const String body = ai_body(c.model, c.url.indexOf("api.deepseek.com") >= 0, g_vi, now, c.instr, transcript);
  String reply;
  const int code = ai_post(c.url + "/chat/completions", c.key, "application/json", (const uint8_t *)body.c_str(), body.length(), reply);
  if (code == 200) {
    const String content = ai_content(reply.c_str());
    r = ai_parse(content);
    Serial.printf("[ai] sorted as %d: %.300s\n", r.kind, content.c_str());
  }
  return code;
}

// ---------- the job: both calls in a task; the loop polls g_job.done ----------
struct AiJob {
  AiCfg cfg;
  size_t wav = 0;
  String now, text;
  AiResult r;
  int code = 0;
  AiFail fail = AF_NONE;
  volatile bool done = true;
};
inline AiJob g_job;

inline void ai_task(void *) {
  AiJob &j = g_job;
  j.code = ai_transcribe(j.cfg, j.wav, j.text);
  if (j.code != 200) j.fail = AF_STT;
  else if (j.text.isEmpty()) j.fail = AF_EMPTY;
  else {
    Serial.printf("[ai] heard: %s\n", j.text.c_str());
    j.code = ai_sort(j.cfg, j.text, j.now, j.r);
    if (j.code != 200) j.fail = AF_AI;
    else if (j.r.kind == AI_BAD) j.fail = AF_REPLY;
    else if (j.r.kind == AI_UNCLEAR) j.fail = AF_EMPTY;  // mumbling, noise: 05d, not 05b
  }
  j.done = true;
  vTaskDelete(nullptr);
}

// Checks what can be checked without the network; on AF_NONE the task is running.
inline AiFail ai_start(size_t wav) {
  g_job = AiJob();
  if (!wav || mic_seconds() < 0.5f) return g_job.fail = AF_SHORT;
  if (WiFi.status() != WL_CONNECTED) return g_job.fail = AF_NONET;
  g_job.cfg = ai_cfg();
  if (g_job.cfg.sttKey.isEmpty() || g_job.cfg.key.isEmpty()) return g_job.fail = AF_NOKEY;
  g_job.wav = wav;
  g_job.now = now_key();
  g_job.done = false;
  if (xTaskCreatePinnedToCore(ai_task, "ai", 12288, nullptr, 1, nullptr, 0) != pdPASS) {
    g_job.done = true;
    g_job.code = -3;
    return g_job.fail = AF_AI;
  }
  return AF_NONE;
}
