# HeySticky — AI voice plan (Phase 2)

Branch `feature/ai`. Spec context: PLAN.md §3 (hold top button = AI), §4 (screens 05–05d), §5 Phase 2, §8 (keys), §13 (`ai_*` strings).

**Flow:** hold top button → **05 Listening** (record while held) → release → **05a Working** (transcribe → sort → save) → **05c Done**. If the AI can't tell to-do from note → **05b Review** (To-do / Note / Record again). Any error → **05d Failed**.

---

## 1. Decisions

- **Provider:** [OpenRouter](https://openrouter.ai), one key for both calls, base URL `https://openrouter.ai/api/v1`. Any OpenAI-compatible server (Groq, OpenAI, Mistral, self-hosted Whisper) works by changing the URL and model names.
- **Speech-to-text:** `openai/whisper-large-v3` via `POST {url}/audio/transcriptions` (OpenAI-style multipart upload, `file` + `model`).
- **Sorting / writing:** `~deepseek/deepseek-flash-latest` (alias that follows DeepSeek's newest Flash model) via `POST {url}/chat/completions`. JSON is asked for in the prompt, not with `response_format: json_schema` (only some OpenRouter models support it).
- **Two services, one key by default** (follows Figma 06h, which has a URL / model / key per section). Flash keys `stt_url`, `stt_model`, `stt_key`, `ai_url`, `ai_model`, `ai_key` (PLAN §8). Defaults: `stt_url` = OpenRouter, `stt_model` = `openai/whisper-large-v3`, `ai_model` = `~deepseek/deepseek-flash-latest`; an empty `ai_url` / `ai_key` means "same as speech-to-text", so one OpenRouter key is pasted once. Defaults live in `net.h` (`AI_URL_DEF`, `STT_MODEL_DEF`, `AI_MODEL_DEF`).
- **Mixed English / Vietnamese:** never send a `language` parameter (Whisper picks one language per 30 s window). English words inside Vietnamese sentences usually survive; whole-sentence switches may come out translated or garbled. Measured in AI-09.
- **Recording:** 16 kHz, 16-bit mono PCM in PSRAM (32 KB/s). Max **60 s** (1.9 MB; OpenRouter/OpenAI file limit is 25 MB). Under **0.5 s** → "I didn't catch that".
- **No on-device speech recognition:** Espressif's ESP-SR only does wake words / command lists and has no Vietnamese.

## 2. Hardware (to confirm on this unit)

| Function | GPIO |
|---|---|
| PDM mic clock | 19 |
| PDM mic data | 20 |
| Mic power enable (high = on, ~10 ms before reading) | 38 |

Source: `board_reterminal_sticky.c` in facebookincubator/muse-gadget-sdk (Apache-2.0, by a Seeed engineer, citing Seeed's factory firmware). No clash with PLAN §1. GPIO19/20 are the S3's native USB pins, unused here because USB-C goes through the UART bridge. Driver: ESP-IDF `driver/i2s_pdm.h`, PDM RX on `I2S_NUM_0`.

## 3. Cost (prices checked 2026-11-17)

Assumes 20 clips a day × 15 s ≈ 150 min a month.

| Part | Price | ≈ per month |
|---|---|---|
| `openai/whisper-large-v3` | $0.000008/s ($0.00048/min) | $0.07 |
| `~deepseek/deepseek-flash-latest` | $0.036 in / $0.50 out per 1M tokens; ~800 in + ~300 out per clip | $0.12 |
| OpenRouter credit purchase fee | 5.5 %, $0.80 minimum per purchase | — |

Alternatives on OpenRouter worth testing for Vietnamese: `qwen/qwen3-asr-1.7b` ($0.000008/s), `mistralai/voxtral-mini-transcribe-3-0` ($0.000058/s), `openai/gpt-transcribe` ($0.000075/s). Avoid `whisper-1` and `gpt-4o-*-transcribe`: OpenAI shuts them down on 2027-02-26.

## 4. Sorting prompt and reply

The chat call gets: today's date, weekday, local time, the system language (en / vi), the user's note instructions (`/ai/note-instructions.txt`, max 2,000 characters) and the transcript. It must answer with one JSON object, nothing else:

```json
{"type": "todo",   "todos": [{"text": "Call the dentist", "due": "2026-11-18 09:00"}]}
{"type": "note",   "title": "Idea: habit grid", "body": "…"}
{"type": "unsure", "transcript": "…"}
```

`due` is `YYYY-MM-DD HH:MM`, `YYYY-MM-DD`, or absent. Text comes back in the system language (`Output language: English / Vietnamese`), translated from whatever was spoken, so mixed memos end up in one language the fonts can draw. `{"type":"unclear"}` = mumbling, noise, or speech-to-text inventing text (a short Chinese / Japanese / Korean phrase, "Thank you for watching") → 05d. Known gap (tested 2026-11-18): a fluent fake like "明天上午9点，9号楼" still becomes a to-do; a loudness gate before upload would catch it. The parser strips code fences and ignores text around the object; anything unreadable → 05d "AI service error".

- To-dos → appended to `/todo.md` with `@due(…)` through `todo.h`.
- Notes → a new `/notes/NNN.md` through `notes.h`. With **Plain text** on (06h toggle), the transcript is saved as the note body and the chat call is only used to decide to-do vs note.

## 5. Errors (05d)

| Cause | Message (§13) |
|---|---|
| No Wi-Fi / no internet | No internet connection |
| `ai_key` empty | AI key missing — open Web setup |
| HTTP status ≠ 200, timeout, bad JSON | AI service error ({code}) |
| Too short, silent, or empty transcript | I didn't catch that |

05d buttons: Try again (back to 05) and Home. Fail beep / success beep when Sound is on.

## 6. Tasks

- [x] AI-01 `mic.h`: PDM RX init, power on/off (GPIO38), record into a PSRAM buffer, running level for the waveform. Bring-up: log levels over serial and write a test clip to `/ai/last.wav`; confirm pins and audio on the device. Pins confirmed 2026-11-17. clip01 (raw, 5.9 s, English) → OpenRouter `openai/whisper-large-v3` playground: "Hello, hello, how are you? Do you hear me?" (exact). Raw PDM sits ~1500 off zero with speech only ~10 dB over the floor, so `mic.h` adds a ~13 Hz DC blocker, ×2 gain (`MIC_GAIN`; ×4 peaked at −3 dBFS on the device) and a 300 ms settle: the same clip filtered offline gives speech −25 dBFS, noise −58 dBFS, no clipping. Filtered build checked on the device
- [x] AI-02 Hold top button: `BTN_TOP_LONG` opens 05 Listening and records until release (pin read directly); short press still = Home; the sleep-wake press still does nothing else; never sleep during Listening / Working
- [x] AI-03 05 Listening: waveform strip (260×80) from the mic level, flat when silent; full refresh on leaving. Device-tested 2026-11-18. A partial refresh takes ~860 ms, so the bars move about once a second, not 3×/s. Bars = the Figma Step=1 shape scaled by the loudest mean level since the last update (`WAVE_DB_FLOOR` −62 → flat, `WAVE_DB_FULL` −32 → full) and rotated each step. Title uses the built-in ExtraBold 40 (Figma: 36, no such slot). Mic icon drawn from the Figma SVG into `tools/icons/mic85.png`. Touches and UP/DOWN are ignored while listening; the take returns to the screen it started from until AI-04..06
- [x] AI-04 `ai.h` transcription: WAV header + multipart body built in PSRAM, HTTPS POST with the built-in CA bundle, read `text`. Device-tested 2026-11-18 (EN, VI): the body is built in place around the take (`MIC_PRE` / `MIC_POST`); both calls run in `ai_task` (core 0, 12 KB stack). Upload takes 4.5–10 s (~37 KB/s HTTPS), the slow part of the flow
- [x] AI-05 `ai.h` sorting: chat request (§4), parse the reply, save to-dos / note; 05a Working steps (Heard you → Sorting it out… → Saving…). Device-tested 2026-11-18. DeepSeek ~1 s with `thinking` disabled (~4 s with it). Saving runs on the main loop (the card shares the panel's SPI bus). Replies come back in the system language; `unclear` (noise, fake subtitles) → 05d. A mumble once came back as Chinese text, which the fonts can't draw ("9 … 9" to-do), hence the translate rule (§4)
- [x] AI-06 05b Review (transcript + To-do / Note / Record again), 05c Done (Added + list), 05d Failed (§5); EN + VI from Figma. Device-tested 2026-11-18. 05b saves the transcript as is (no AI). 05c buttons: Go to To-do / Go to Notes + Done (Figma 14:452 still says Try again). 05d Try again resends the same take for network / service failures
- [x] AI-07 Host test `tools/ai_test.cpp`: WAV header, multipart body, JSON reply parsing (plain, fenced, wrapped in text, broken, each `type`). Needs ArduinoJson on the include path (README)
- [x] AI-08 Phone page 06g Integrations → 06h AI (Figma, EN + VI): Speech-to-text and AI sections (Base URL, Model, API key each), Note instructions (type or upload .txt, 2,000 characters, saved to `/ai/note-instructions.txt`); Integrations row + keys footer on 06; `/api/ai` GET / POST. Keys come back only as a hint (`sk-or-••••`); an empty key field keeps the saved key. Notion, Home Assistant, Remote access stay hidden (Phase 3). Device-tested 2026-11-18 (key saved from the phone, shown masked)
- [ ] AI-09 Device test: 5–10 clips each in English and Vietnamese (to-do, note, unsure, deadlines like "tomorrow at 9"); compare the saved clips against the §3 alternatives with `curl`; then stop writing `/ai/last.wav`
- [ ] AI-10 Docs: PLAN §2, §5, §8, §12 and the Phase 2 requirement list; README (how to get an OpenRouter key, cost, privacy note: audio goes to OpenRouter and the chosen model provider)
- [ ] AI-11 Release v1.1.0: `FW_VERSION`, `tools/release.sh v1.1.0`, GitHub Release, OTA test from v1.0.2
- [ ] AI-12 Seeed registry: refresh the prepared entry to v1.1.0, add the device photo, `npm test` + `npm run validate`, then wait for confirmation before fork / PR / form

## 7. Open

- [x] Figma: check that 05–05d frames exist in both languages before AI-03 / AI-06. Checked by the user 2026-11-18: EN and VI frames exist
- [x] Mic gain: ×2 after a DC blocker (AI-01); recheck with Vietnamese clips spoken at arm's length in AI-09
- [x] "Plain (no AI)" → **Plain text** toggle on 06h under the AI key, plus a **Note instructions** toggle (hides the box; disabled while Plain text is on). Figma 06h EN + VI updated 2026-11-18. Plain notes drop characters the fonts can't draw; nothing left → 05d
- [ ] No way to delete a saved key yet (only replace it)
- [ ] AI-09: add mixed English / Vietnamese clips to the model comparison
- [ ] Battery cost of one clip (mic + Wi-Fi + TLS); read it from the gauge during AI-09
