// PDM microphone (AI-01): 16 kHz 16-bit mono into a PSRAM WAV buffer, recorded by its own task so
// e-paper refreshes (~860 ms) never drop audio. Pins: PLAN_AI.md §2.
#pragma once
#include <ESP_I2S.h>
#include <wav_header.h>
#include "pins.h"

constexpr uint32_t MIC_RATE = 16000;
constexpr uint32_t MIC_MAX_S = 60;
constexpr size_t MIC_MAX_PCM = MIC_RATE * 2 * MIC_MAX_S;  // 1.92 MB
constexpr uint32_t MIC_SETTLE_MS = 300;  // PDM filter warm-up, thrown away (clip01: first 250 ms was a transient)
constexpr float MIC_GAIN = 4;            // calibration knob: clip01 speech sat at about -35 dBFS RMS, +12 dB
constexpr float MIC_HP = 0.995f;         // DC blocker pole (~13 Hz); the raw PDM output sits ~1500 off zero

inline I2SClass g_mic;
inline uint8_t *g_wav = nullptr;      // 44-byte header + PCM
inline volatile size_t g_pcmLen = 0;  // PCM bytes recorded so far
inline volatile int g_micLevel = 0;   // mean |sample| of the last 32 ms chunk (0..32767)
inline volatile int g_micPeak = 0;    // loudest |sample| of the whole take
inline volatile bool g_micRun = false;
inline TaskHandle_t g_micTask = nullptr;

inline void mic_task(void *) {
  int16_t chunk[512];  // 32 ms
  float px = 0, py = 0;  // DC blocker state: y = x - x[-1] + MIC_HP * y[-1]
  bool primed = false;
  uint32_t t0 = millis();
  while (g_micRun) {
    size_t n = g_mic.readBytes((char *)chunk, sizeof(chunk));
    if (!n) continue;
    if (!primed) { px = chunk[0]; primed = true; }
    long sum = 0;
    int peak = g_micPeak;
    for (size_t i = 0; i < n / 2; i++) {
      const float x = chunk[i], y = x - px + MIC_HP * py;
      px = x; py = y;
      chunk[i] = (int16_t)constrain(y * MIC_GAIN, -32768.0f, 32767.0f);
      int a = abs(chunk[i]);
      sum += a;
      if (a > peak) peak = a;
    }
    if (millis() - t0 < MIC_SETTLE_MS) { g_micPeak = 0; continue; }  // filters settle; keep nothing
    g_micLevel = sum / long(n / 2);
    g_micPeak = peak;
    n = min(n, MIC_MAX_PCM - g_pcmLen);
    memcpy(g_wav + PCM_WAV_HEADER_SIZE + g_pcmLen, chunk, n);
    g_pcmLen += n;
    if (g_pcmLen >= MIC_MAX_PCM) break;  // full: mic_full() tells the caller
  }
  g_micTask = nullptr;
  vTaskDelete(nullptr);
}

inline bool mic_full() { return g_pcmLen >= MIC_MAX_PCM; }
inline float mic_seconds() { return g_pcmLen / float(MIC_RATE * 2); }

inline void mic_power(bool on) {
  pinMode(PIN_MIC_PWR, OUTPUT);
  digitalWrite(PIN_MIC_PWR, on ? HIGH : LOW);
  if (on) delay(10);
}

inline bool mic_start() {
  if (!g_wav) g_wav = (uint8_t *)ps_malloc(PCM_WAV_HEADER_SIZE + MIC_MAX_PCM);
  if (!g_wav) { Serial.println("[mic] no PSRAM buffer"); return false; }
  g_pcmLen = 0; g_micLevel = 0; g_micPeak = 0;
  mic_power(true);
  g_mic.setPinsPdmRx(PIN_MIC_CLK, PIN_MIC_DATA);
  if (!g_mic.begin(I2S_MODE_PDM_RX, MIC_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("[mic] I2S begin failed");
    mic_power(false);
    return false;
  }
  g_mic.setTimeout(100);
  g_micRun = true;
  xTaskCreatePinnedToCore(mic_task, "mic", 4096, nullptr, 5, &g_micTask, 0);
  return true;
}

// Stops recording, powers the mic off and fills in the WAV header. Returns the WAV size (0 = nothing).
inline size_t mic_stop() {
  g_micRun = false;
  while (g_micTask) delay(5);
  g_mic.end();
  mic_power(false);
  if (!g_wav || !g_pcmLen) return 0;
  const pcm_wav_header_t h = PCM_WAV_HEADER_DEFAULT(g_pcmLen, 16, MIC_RATE, 1);
  memcpy(g_wav, &h, PCM_WAV_HEADER_SIZE);
  return PCM_WAV_HEADER_SIZE + g_pcmLen;
}

// AI-01 bring-up card in mid-screen (05 Listening replaces it in AI-03); stays until the next redraw.
inline void mic_card(const String &a, const String &b) {
  display.fillRect(40, 330, 400, 140, PAPER);
  display.drawRect(40, 330, 400, 140, INK);
  display.drawRect(41, 331, 398, 138, INK);
  ui_text(F_X30, a, W / 2, 352, 40, AL_CENTER);
  ui_text(F_R25, b, W / 2, 412, 33, AL_CENTER);
  ui_partial(40, 330, 400, 140);
}

// AI-01 bring-up: hold the top button to record, release to save /ai/last.wav and log the levels.
inline void mic_bringup() {
  if (!mic_start()) { mic_card("Mic error", "I2S did not start"); return; }
  Serial.println("[mic] recording (release the top button to stop)");
  mic_card("Recording...", "Release to stop");
  uint32_t lastLog = 0;
  while (digitalRead(PIN_BTN_TOP) == LOW && !mic_full()) {
    if (millis() - lastLog >= 250) { lastLog = millis(); Serial.printf("[mic] level %d\n", (int)g_micLevel); }
    delay(10);
  }
  const size_t size = mic_stop();
  Serial.printf("[mic] %.1f s, peak %d, %u bytes\n", mic_seconds(), (int)g_micPeak, (unsigned)size);
  const String stats = String(mic_seconds(), 1) + " s, peak " + String(int(20 * log10f(max((int)g_micPeak, 1) / 32768.0f))) + " dB";
  if (!size || !g_sdOk) { mic_card(size ? "No SD card" : "Nothing recorded", stats); return; }
  if (!SD.exists("/ai")) SD.mkdir("/ai");
  File f = SD.open("/ai/last.wav", FILE_WRITE);
  const size_t w = f ? f.write(g_wav, size) : 0;
  f.close();
  Serial.printf("[mic] /ai/last.wav %s\n", w == size ? "saved" : "write failed");
  mic_card(w == size ? "Saved last.wav" : "SD write failed", stats);
}
