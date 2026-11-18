# HeySticky — Project Plan

Personal assistant firmware for the **Seeed reTerminal Sticky** (ESP32-S3, 3.97" e-paper).
Home dashboard, to-dos with deadlines, notes on microSD, a phone setup page, and hold-to-talk AI. HeySticky V1 ships in three phases: 1 Core, 2 AI voice, 3 Integration (§5).

- **Design (clickable prototype):** https://www.figma.com/design/SHu2RsGVk0D3wuorPdUztE (English section + a Tiếng Việt copy to its right; strings in §13)
- **Display library:** https://github.com/Seeed-Studio/Seeed_GFX2 (`Seeed_Product::reTerminal_Sticky`)
- **Publishing target:** https://github.com/Seeed-Projects/reterminal-sticky-playground-registry (community firmware, firmware-only)

---

## 1. Hardware

| Item | Detail |
|---|---|
| MCU | ESP32-S3R8, 8 MB octal PSRAM, 32 MB flash |
| Display | 3.97" 800×480 e-paper, **SSD1677 or SSD2677** (auto-detected by Seeed_GFX2), used in portrait 480×800, 1-bit |
| Touch | GT911 capacitive |
| Buttons | **Top (OK / AI)** GPIO4 · **UP** GPIO5 · **DOWN** GPIO6 |
| Sensors | SHT40 temp/humidity, BQ27220 battery gauge, RTC, IMU (sensor I2C: SCL 0 / SDA 1) |
| Other | Buzzer GPIO48, PDM microphone, microSD (shares e-paper SPI) |

Pin map (from the open-source sticky-2048 firmware):

| Function | GPIO |
|---|---|
| E-paper MOSI / CLK / MISO / CS / DC / RST / BUSY / EN | 14 / 13 / 12 / 15 / 16 / 17 / 18 / 47 |
| Touch SCL / SDA / INT / RST / EN | 2 / 3 / 21 / 41 / 42 |
| Power hold / lock | 45 / 46 |
| Charger enable (BQ25616 EN_BAT_CHGn, active low: firmware drives it LOW) / USB present (high) | 39 / 9 |
| Fuel gauge INT (BQ27220 @ 0x55) | 7 |
| microSD CS / EN | 8 / 10 |

## 2. Tech stack

- **Arduino C++** built with `arduino-cli`; core + libraries pinned in `sketch.yaml`
- Board: `esp32:esp32:XIAO_ESP32S3:PSRAM=opi,CDCOnBoot=cdc`. `cdc` means USB CDC *Disabled*, which is what we want: the USB-C port goes through a USB-UART bridge ("USB Single Serial"), so `Serial` must be UART0. The board menu only offers 8 MB flash, so `tools/build.sh` (compile; `-u -p <port>` upload; `-e` export) adds `build.flash_size=32MB` + `default_32MB` (two 12.5 MB OTA slots). Always build through it: OTA can never change the partition table, so a release fixes it for every device
- Seeed_GFX2 is not in the Arduino index: cloned into `libs/Seeed_GFX2` (git-ignored) at a pinned commit, referenced from `sketch.yaml`
- Libraries: **Seeed_GFX2**, **ArduinoJson**, a small QR-code library; core built-ins `WiFi`, `WebServer`, `DNSServer`, `HTTPClient`, `Preferences`, `SD`
- Sensors/touch over raw I2C (no extra libraries)
- Fonts: JetBrains Mono TTFs in `fonts/` (OFL) -> `tools/make_fonts.py` (Pillow) -> VLW headers in `heysticky/src/fonts/`, pre-thresholded to pure black/white because the 1-bit panel maps any non-black pixel to white. Spaces are drawn as U+00A0 (the library guesses U+0020's width). Text is placed by Figma text box: top = box y + (box h - ascent - descent) / 2
- Icons: exported from Figma at 1x into `tools/icons/` -> `tools/make_icons.py` -> 1-bit bitmaps in `heysticky/src/icons.h` (alpha threshold 64 keeps ~1.5 px strokes unbroken)
- Refresh: full on screen change and at boot (after a white clear); partial for clock, weather, indoor, status bar; a full refresh every 30 partials (20 while typing)
- Measured on this unit: a partial refresh takes ~860 ms regardless of area. Touch is read by a FreeRTOS task on core 0 into a queue, so taps made during a refresh are applied in the next one; typing is silent (a beep per key cannot keep pace)
- Services: **Open-Meteo** (weather + city lookup + timezone, no key), **NTP** (time)
- Phase 2: PDM mic (clock GPIO19, data GPIO20, power GPIO38) recorded at 16 kHz mono into PSRAM, max 60 s. Any **OpenAI-compatible** API — speech-to-text (`/audio/transcriptions`) and chat (`/chat/completions`). Default: one [OpenRouter](https://openrouter.ai) key for `openai/whisper-large-v3` + `~deepseek/deepseek-flash-latest`. Both calls run in a FreeRTOS task (`ai.h`) so 05a can animate. Details, cost and tests: `PLAN_AI.md`

## 3. Interaction model

- **Touch** everything. Swipe left/right between pages (Home 1/2 ⇄ Apps 2/2, To-do and Notes pages, a note's pages; right on To-do / Notes page 1 → Apps, on a note's page 1 → Notes).
- **DOWN = back**, same as swiping left-to-right; **UP = forward**, same as swiping right-to-left. In Settings, back goes up one level (sub-screen → parent → Settings → Apps). They only move between pages; in the note editor they hide the keyboard if it is open, otherwise scroll the body (DOWN = earlier, UP = later); elsewhere without pages they do nothing (they are never Cancel).
- **Press top button = Home**, from any screen. **Swipe up from the bottom edge = Home** too (start in the bottom 15 px, y ≥ 785; keys ignore touches that start there).
- **Hold top button = talk to AI** (Phase 2). Sleep is by idle timer, not a button hold.
- **Long-tap a to-do row = edit.** Tap = tick.
- **Note body box scrolls**: swipe up/down inside it moves a box-full (minus one line); a scrollbar shows on the right edge when the text doesn't fit. Typing jumps back to the end.
- Partial refresh for small changes (typing, ticking); full refresh when changing screens.
- No bottom hint bar: screens don't spell out what the buttons do.

## 4. Screens (Figma numbering)

| Row | Screens |
|---|---|
| Home & system | 01 Home · 01a Apps · 01b Sleep (Home with a moon in the status bar) · 01c Home (no tasks today) · 01d Sleep (no tasks today) · 01e Turned off · 01f Battery empty |
| To-do | 02 To-do list · 02a New task (keyboard) · 02b New task (no keyboard) · 02c Date · 02d Edit task (keyboard) · 02e Edit task (no keyboard) · 02f Delete task (confirm) · 02g To-do (empty) · 02h Time · 02i Discard task (confirm) · 02j Discard changes (confirm) |
| Notes | 03 Notes list · 03a New note (keyboard) · 03b New note (no keyboard) · 03c Note view · 03d Edit note (keyboard) · 03e Edit note (no keyboard) · 03f Delete note (confirm) · 03g Notes (empty) · 03h Discard note (confirm) · 03i Discard changes (confirm) |
| Settings & setup | 04 Settings · 04a Language & Font · 04b System Language · 04c User Font · 04d Software Update · 04e Configuration · 04g Temperature · 04h About · 04i SD Card · 04j Web setup (On / Off) · 04k SD Files · 04l Sleep after · 04m Sound |
| Phone setup page (served only on the hotspot) | 06 Setup · 06a City suggestions · 06b Wi-Fi · 06c Enter password · 06d Incorrect password · 06e Network details · 06f Forget network · 06g Integrations · 06h AI · 06h1 Speech-to-text · 06h2 AI for notes & to-dos · 06h3 Testing / Connected / Couldn't connect · 06h4 Delete provider · 06i Notion · 06j Home Assistant · 06k Remote access · 06l Profile · 06m Font book · 06n Add font |

**Layout & type (all Sticky screens; not the phone setup page):**
- Font: **JetBrains Mono** (Regular, Bold, ExtraBold), or the user font picked in 04c for text (digits stay JetBrains Mono). Minimum text size **25 px**; the status bar and the page counter are the only exceptions, at **20 px**.
- Content area: 40 px left/right margin (x 40–440); dividers span that width.
- Status bar (top, 40 px tall) sits **16 px** from the screen edges (x 16–464): "HeySticky" left; hotspot icon (while Web setup is On, Figma StatusBar Hotspot=true) + Wi-Fi (moon when asleep, Mode=Asleep) + bolt (while plugged in, Mode=Charging) + battery % right.
- Page counter (`1/2`, no spaces): centred, 16 px above the bottom edge, **only on screens with more than one page** (Home/Apps, To-do, Notes, Note view). No hint bar.
- Bottom buttons sit **16 px** above the bottom edge; on screens with a page counter they sit 12 px above it (bottom at y 746).
- To-do rows: checkbox, then the task title with its due date on the line below. Notes list: title on one line, cut with "…".
- Task deadline: under DEADLINE (OPTIONAL) the editor has two rows, `Date Off ›` and `Time Off ›` (values `17/11/26`, `09:00` once set). Date (02c) = toggle + calendar + Done; Time (02h) = toggle + HH:MM fields + number pad (Done on the pad). Cancel undoes that screen. Date only = date-only `@due`; Time only = today at that time; both off = no deadline.
- Editing: every keyboard's last key is **↵** (no "Done" text; the 02h time keypad too). In one-line fields (task, note title) ↵ only hides the keyboard; in the note body ↵ = new line and UP/DOWN hide the keyboard. Tap a field to bring the keyboard back. The cursor is always at the end of the text. Shift turns on by itself at the start of a field or line and after ". " / "! " / "? " (one letter, like a phone). Also like a phone: two spaces after a word make ". ", and a space on the 123 page goes back to letters. Empty fields show an Italic placeholder (*Task*, *Title*, body hint); a focused field shows it with the cursor after it until tapped or typed in. Edit task / Edit note have **Save** and **Delete** buttons; New task / New note have **Save**. Delete always opens a confirmation (Cancel / Delete). Home (top button or bottom-edge swipe) in a task or note editor (incl. the task's Date/Time) asks first when work would be lost: a New task / New note with text typed → 02i / 03h *Discard task? / Discard note?*; an Edit that changed the text, title, body or deadline → 02j / 03i *Discard changes?*. Discard = drop it and go Home (the saved item stays as it was), Cancel = back exactly as it was. Nothing typed / nothing changed: Home goes straight there. On the warning the top button does nothing.

**Settings (04):** Auto-rotate (toggle) · Sound On/Off › (off by default) · Language & Font › · Software Update › · Configuration › · Sleep after › · Web setup › · About ›. Same layout as Seeed's official Sticky firmware, plus Sleep after and Web setup.
- **Sound (04m):** On/Off toggle in the top bar (like Web setup); when On, **Volume** Low / Medium / High checkboxes (flash key `vol`, default High). Tapping a level saves it and plays a sample. Volume = buzzer PWM duty (`VOLUME_DUTY` in `hw.h`: 40 / 150 / 512 of 1024; High = 50 %, the loudest a pin can drive). Every tap, swipe and button beep is `BEEP_MS` = 120 ms at 2.7 kHz; every page or screen change beeps (swipe or UP/DOWN alike, nothing to move = silent); "can't do that" beeps are low (1.2 kHz: Save refused, invalid time digit). The power tunes use the chosen volume even with Sound off.
- **Sleep after (04l):** 1 / 2 / 5 / 10 min / Never checkboxes (flash key `sleep_min`, default 2). After that long with no touch or button, on battery and on USB: Home with the moon (01b / 01d), then light sleep with Wi-Fi and touch off and the CPU at 80 MHz. A timer wakes it just after each minute to update the clock (partial refresh); indoor temp/humidity every 10 min; every 30 min Wi-Fi comes on for weather and an NTP reply (light sleep's clock drifts), then off. Only the top button wakes it fully, to Home (that press does nothing else). Never sleeps while a task or note editor (incl. Date, Time, Discard) is open, while Web setup is On, or while an update is checking or installing. Serial logs `[sleep] in`, `[sleep] tick hh:mm`, `[sleep] sync …` and, after waking, the battery used (`[sleep] last: … mAh, … mA avg`, from the gauge's remaining capacity; repeated in the minute log).
- **Shutdown (01e / 01f), like Seeed's:** hold **UP + DOWN** together for 3 s on any awake screen → off tune (slow falling E‑B‑E) → 01e *Turned off* / *Hold the top button to turn on.* → power hold pin low (on battery the power is cut). On battery at ≤ 3 % (`BATTERY_EMPTY_PCT`, two readings a minute apart, awake or asleep, not during an update install) → 01f *Battery empty* / *Charge the Sticky to turn it on.* → off. UP + DOWN pressed together never pages back or forward. The message stays on the e-paper. Still powered (USB): deep sleep (charger and touch pins held low) until the top button. **Turning on:** a start by the button (on battery, or a wake from USB off) needs the top button held 2 s (`ON_HOLD_MS`) → on tune (quick rising C‑E‑G‑C) → on; a tap, a touch or another button goes straight back off with no screen change. USB plug-in, reset and flashing start normally. Both tunes (`TUNE_ON` / `TUNE_OFF` in `hw.h`) play even with Sound off. Boot log: `[power] reset N, wake N`.
- **Language & Font (04a):** System Language › and User Font ›. **04b System Language:** English / Tiếng Việt checkboxes; every string on the Sticky and on the phone page follows it (§13). **04c User Font:** "Default (built-in)" + up to 8 fonts found in `/fonts/` on the microSD. Tapping a font ticks it at once and shows a "Loading..." card while it loads; tapping the font in use does nothing. A user font replaces the 13 text font slots (R20/25/30, B20/25/26/28/35, X25/30/32/40, I25); the digit-only fonts (clock, temperatures, 04j address) stay built-in. A missing or bad slot file falls back to the built-in font.
- **Font book (06m) / Add font (06n), phone page:** 06m lists **JetBrains Mono (built-in)** and the fonts on the card, with an *In use* tag on the active one and a trash button on each card font (asks first). 06n picks a TTF/OTF per style (Regular, Bold, ExtraBold, Italic); the browser renders it 1-bit (threshold 128, same characters as `tools/make_fonts.py`) into the 13 VLW slots and uploads them. A missing style warns and uses Regular; a font without Vietnamese letters warns. A 1-bit preview shows each style. Fonts are added only from the phone page; Seeed `.fpack` files are not read.
- **Software Update:** current version, "up to date" / "update available", **Check for updates** button (OTA, see §9). Figma 04d shows only the up-to-date state; the others reuse its layout: Not checked yet / No Wi-Fi connection, Checking…, "v1.1.0 is available" + **Install v1.1.0**, Installing v1.1.0… 40% (refreshed every 10%, no button), Couldn’t check for updates, Update failed + **Try again**.
- **Configuration:** City (display only, set on the phone setup page) · Timezone (UTC±h from the last weather reply, read-only) · Refresh interval (30 min, fixed) · Temperature › (°C / °F checkboxes; changing it fetches the weather again).
- **Phone setup page (06–06k), like iPhone Settings:** served only on the Sticky's hotspot (04j), at `192.168.4.1`. Full phone width, JetBrains Mono (Regular + Bold, served by the Sticky; SemiBold text uses Bold, Figma too), long titles shrink to fit beside ‹. Every page opened is a browser history entry, so the phone's swipe back, the browser back and ‹ all go up one page. The page is sent with `Cache-Control: no-store`, so a new firmware's page shows on the next reload. **06 Setup:** four rows — **Profile** "{name}" ›, **Wi-Fi** "{current network}" ›, **Font book** › (06m, no value shown), **Integrations** ›. Sub-pages have the title on the left and a circled ‹ back button on the right. **06l Profile:** Your name, City (06a suggestions) and **Save to Sticky** (name + city; no restart). **06b Wi-Fi:** ✓ current network, **MY NETWORKS** (saved, up to 5, ⓘ), **OTHER NETWORKS** (the Sticky's scan, with 🔒 and signal 1–3), **Other…** (hidden network: name + password). Tap a saved or open network → join; a new secured one → 06c sheet like iOS (round ✕ = cancel and ✓ = join, Wi-Fi icon, *Join “{ssid}”*, *Enter the password to join this Wi-Fi network.*, pill Password field). The row shows "Joining…"; the Sticky tests the network while its hotspot stays up, so the phone keeps the page: success → ✓ on that row (saved, first in the list); router rejects the password → 06d *Incorrect password for "{ssid}"* (OK); not found / timeout → "Couldn't join". ⓘ → 06e details: Status, Signal, Security, IP address, **Forget This Network** → 06f *Forget "{ssid}"?* (Cancel / Forget). **06g Integrations:** AI (06h); Notion (06i), Home Assistant (06j) and Remote access / Tailscale (06k) stay hidden until Phase 3. **06h AI:** two rows, **Speech-to-text** and **AI for notes & to-dos**, each showing its saved model (or *Not set*) ›. **06h1 / 06h2** (one page per service, OPENAI-COMPATIBLE tag + description): a dashed **+ Add model provider** box → a form with Base URL, Model, API key and **Cancel / Save** → a saved line `{model}` with edit ✎ and delete 🗑. On 06h2 the URL and key may stay empty (*Same as speech-to-text*). **Save** shows 06h3 *Testing connection…* while the Sticky sends a tiny request (§9), then *Connected* (✓, OK → saved line) or *Couldn't connect* (✕, the reason, OK → the form stays filled in). Nothing is saved unless the test passes. Edit opens the form filled in; an empty key field keeps the saved key. Delete → 06h4 *Delete speech-to-text?* / *The API key will be removed from the Sticky.* (Cancel / Delete) → back to the dashed box. Below the 06h2 provider: **Plain text** and **Note instructions** toggles + the instructions box (type or upload .txt). No Save button: toggles save as they flip, the instructions when the box loses focus or a .txt is loaded.
- **City suggestions (06a):** from 2 letters, after a 0.3 s pause, up to 5 places ("{name}" + "{region}, {country}"). The Sticky looks them up through its own Wi-Fi; if it has no internet yet, the phone tries Open-Meteo itself; if neither works there is no list and the typed name is used (top match). Picking one also sends its lat/lon, so the Sticky never guesses between same-name towns. Units is set only on the Sticky (04g).
- **Web setup (04j):** On/Off toggle in the top bar. **On** starts the hotspot `HeySticky-Setup` with a new random 8-character WPA2 password each time and shows: *Scan to join* + a Wi-Fi QR code (`WIFI:T:WPA;S:HeySticky-Setup;P:{password};;` — the phone camera joins it), "Wi-Fi HeySticky-Setup", "Password {password}", *Then open* `192.168.4.1` ("It may open by itself": captive portal). The Sticky stays on its home Wi-Fi while the hotspot is on (clock and weather keep going). **Off** stops the hotspot and the page; nothing below the top bar. Starts Off after boot, except with no saved network (first setup): then On by itself. The setup page is never served on the home network.
- **About:** Name, SN (eFuse BLOCK3, 18 digits), MAC, Software version, Certifications, SD card › (04i: Inserted, Mounted, Path, Space, Files › → 04k SD Files: every file by path with its size, 9 per page).
- **Auto-rotate:** setting is stored; no screen supports rotation yet (needs the IMU).
| AI voice (Phase 2) | 05 Listening · 05a Working · 05b Review (only when unsure) · 05c Done · 05d Failed |

**Home:** "Hello, {name}", big clock + date, weather card (condition, temp, high/low, city), indoor temp + humidity (SHT40), Today card (number of to-dos due today, e.g. "3 tasks today"; "No tasks today" when none; tap → To-do), app dock (To-do, Notes, Home*, Reader*, Settings). *V3.

## 5. Phases (all V1)

### Phase 1 — Core (offline-capable, Wi-Fi for time & weather)
Wi-Fi + phone setup page, Home dashboard, To-do with optional deadlines, Notes, Settings/About, power management, release `.bin`.

### Phase 2 — AI voice
Built on `feature/ai` (2026-11-18); device test AI-09 and release AI-11 still open (`PLAN_AI.md`). Set up on the phone page (06h): add a speech-to-text and an AI provider; each is tested before it is saved.
Hold top button → record → speech-to-text → **Working** → **Done**. Only when the AI can't tell whether it's a to-do or a note does it show **Review** (transcript; choose **To-do** or **Note**, or **Record again**), then Done.
- **To-do:** AI splits the sentence into items and extracts deadlines ("tomorrow at 9" → `@due`).
- **Note:** AI writes the note following the user's **Note instructions** (outline/template/rules, typed or uploaded as .txt on the phone setup page, max 2,000 characters); **Plain text** (06h toggle) saves the transcript as-is; the AI only picks to-do or note.
- Done / Failed screens with the reason, plus success/fail beeps (if Sound is on).
- Listening wave: while recording, partial-refresh only the wave strip (260×80) about 3×/s, bar heights from the mic level; bars go flat when silent. Full refresh when leaving Listening. Figma: `Waveform` component, Step 1→4 looping every 0.35 s.

### Phase 3 — Integration (each its own feature branch)
Notion daily sync (to-dos, notes, and voice recordings passed to the phone over Bluetooth, then deleted from the card) · Home Assistant dashboard · Reader · deadline reminder beeps (wake from sleep at due time) · Tailscale (experimental, community ESP-IDF libs only).
- **Bluetooth (BLE) link to a phone app** (noted 2026-11-17): the ESP32-S3 has BLE 5 and can run it next to Wi-Fi. Seeed's factory firmware uses it for "Bluetooth connect" with the SenseCraft app (notes, to-dos, photos, fonts, OTA). For us: send fonts, notes and to-dos from our own app without the hotspot. The app converts TTF/OTF into the 13 `.vlw` slots (the Sticky has no TrueType renderer); about 1.1 MB per font, so roughly 20–40 s at a realistic 30–60 KB/s. iPhone needs a native app (Safari has no Web Bluetooth); Chrome on Android / desktop could reuse the phone page's converter via Web Bluetooth. Costs a few hundred KB of flash for the BLE stack.

## 6. Requirements

### Phase 1

- [x] REQ-01 First boot with no Wi-Fi → hotspot `HeySticky-Setup`, Web setup screen shows `192.168.4.1` + QR
- [x] REQ-02 Phone setup page saves Wi-Fi, name, city (with suggestions) — stored on the device only
- [x] REQ-03 Home shows greeting, time/date, weather, indoor temp/humidity, Today count, dock
- [ ] REQ-04 Time from NTP; weather every 30 min; no Wi-Fi → weather card says "Offline", everything else works
- [x] REQ-05 To-do: add, tick, edit (long-tap), save, delete with confirmation, optional deadline (Date and Time, each on/off); overdue first, then by due date
- [x] REQ-06 Notes: create, list, view, edit, save, delete with confirmation
- [x] REQ-07 On-screen keyboard with partial refresh
- [x] REQ-08 Settings: Auto-rotate, Sound, Language & Font, Software Update, Configuration (city shown, auto timezone, 30-min refresh, °C/°F), About
- [ ] REQ-09 Touch works on every screen; DOWN/UP page back/forward, top button and bottom-edge swipe go Home (§3)
- [ ] REQ-10 Idle → sleep; wake each minute to redraw the clock; top button wakes fully
- [x] REQ-11 No secrets in source or in the `.bin`
- [ ] REQ-12 Missing SD card → "No microSD card" screen with retry (built in TASK-08, interim layout; not yet tested with the card removed)
- [x] REQ-13 Release package: bootloader `0x0`, partition table `0x8000`, boot_app0 `0xE000`, app `0x10000` + `manifest.json` (`tools/release.sh`); a merged image padded to 32 MB is too big to publish
- [x] REQ-14 OTA: on Wi-Fi connect (at most hourly) and on "Check for updates", compare with the latest GitHub Release; on **Install**, download, verify and install the app `.bin` into the spare OTA slot; reboot; roll back if the new image fails to start. SD data untouched
- [x] REQ-15 Language English/Vietnamese; user fonts from `/fonts/<Name>/<slot>.vlw` (added on the phone page); built-in font covers Vietnamese diacritics
- [x] REQ-17 Wi-Fi set up like iPhone Settings on the phone page (via the 04j hotspot): list nearby networks, join with a password while the page stays connected, wrong-password alert, up to 5 saved networks joined automatically in the background (strongest in range), forget a network
- [ ] REQ-16 About shows name, SN, MAC, software version, certifications and live SD card status (inserted, mounted, path, free space, file count)

### Phase 2 (`PLAN_AI.md`)

- [x] REQ-20 Hold the top button to record (max 60 s); release sends it; under 0.5 s → *I didn't catch that*
- [x] REQ-21 Speech-to-text, then the AI saves to-dos (with deadlines) or a note; 05b Review only when it can't tell; 05c Done / 05d Failed with the reason; beeps if Sound is on
- [x] REQ-22 Everything is written in the system language; noise or invented text → 05d, not a to-do (known gap: a fluent fake still passes)
- [x] REQ-23 Phone page 06h: one page per service; Save tests the connection and saves only on success; edit and delete; keys never go back to the browser
- [x] REQ-24 Note instructions (typed or .txt, max 2,000 characters, on the card) and Plain text, saved as they change
- [ ] REQ-25 Device test: English and Vietnamese clips, deadlines, Review, Failed, the 06h test / edit / delete (AI-09)

## 7. Phase 1 task list

- [x] TASK-01 Pin core + libraries in `sketch.yaml`; compile Seeed's Sticky HelloWorld
- [x] TASK-02 Flash HelloWorld; confirm on the real screen (needs device on USB-C)
- [x] TASK-03 `hw.h`: touch, buttons, buzzer, SHT40, battery — verified over serial
- [x] TASK-04 `net.h`: settings in flash, Wi-Fi connect, hotspot fallback, web setup page
- [x] TASK-05 Clock (NTP + timezone) and weather (Open-Meteo)
- [x] TASK-06 Home screen
- [x] TASK-07 On-screen keyboard
- [x] TASK-08 To-do: list, add, tick, edit, delete, deadlines (Date + Time screens), `/todo.md` on microSD
- [x] TASK-09 Notes: list, view, new, edit, delete
- [x] TASK-10 Settings, Configuration, Temperature, About, Web setup screens. Sound toggle defaults **off**: wire it to `g_sound` in `hw.h` (all beeps are muted until then)
- [x] TASK-15 Wi-Fi (`feature/wifi`): saved-network list in flash (up to 5, migrate the old single network), non-blocking boot + background auto-join and reconnect (last UTC offset kept in flash so the clock and Timezone are right before the first weather reply), 04j hotspot with random WPA2 password + Wi-Fi QR (AP+STA), network scan cached just before the hotspot starts (test refresh while it runs), phone pages 06–06f + 06l as one page with JSON calls (status, scan, join + result, forget, city), test-join with wrong-password detection from the disconnect reason, page served only on the hotspot
- [x] TASK-13 OTA (`feature/ota`): 32 MB two-slot OTA partition table (`tools/build.sh`), version check against GitHub Releases, `HTTPUpdate` install, rollback, Software Update screen
- [x] TASK-14 Language & Font (`feature/lang`): `TR(en, vi)` string table on the Sticky and `L(en, vi)` on the phone page, 04a/04b/04c screens, user fonts as `/fonts/<Name>/<slot>.vlw` (checked by `vlw_ok()`, read into PSRAM), phone 06m Fonts + 06n Add font (TTF/OTF converted in the browser), `/api/fonts`, `/api/font`, `/api/font_del`
- [ ] TASK-11 Power (`feature/power`): Sleep after (04l, `sleep_min`), idle light sleep, minute clock wake, 30-min Wi-Fi sync, top-button wake, sleep battery report, UP + DOWN shutdown and battery-empty shutdown with 01e / 01f (`power.h`)
- [x] TASK-12 README (controls, SD layout, device test record), LICENSE (MIT), `tools/release.sh` (four flash files + manifest), public repo TaDuy2005/heysticky (history scrubbed: no-reply email, sample SN; gitleaks clean)

## 8. Data formats

**microSD** (plain text, readable on any computer):

```
/todo.md            - [ ] Call the dentist @due(2026-11-17 09:00)
                    - [ ] Buy milk
                    - [x] Flash HelloWorld
/notes/012.md       # Idea: sticky habit grid
                    Show a 7×5 grid …
/ai/note-instructions.txt  (Phase 2) how the AI writes notes, set on the phone setup page
/ai/last.wav        (Phase 2, development only) the last recording, overwritten each take; removed in AI-09
/fonts/<Name>/<slot>.vlw  user font, one file per slot (R20 … I25), made by the phone page (06n)
```

**Device flash (`Preferences`)** — never in source, never in the `.bin`:

| Key | Phase 1 | Phase 2 |
|---|---|---|
| `wifi0`…`wifi4` = saved networks (SSID + password), most recently joined first (TASK-15; replaces `wifi_ssid`, `wifi_pass`) | ✓ | |
| `name`, `city`, `lat`, `lon`, `tz`, `utc_off` (last UTC offset, seconds), `units`, `geo_city` (city that lat/lon belong to) | ✓ | |
| `sound`, `vol`, `sleep_min` | ✓ | |
| `rotate`, `lang`, `font`, `ota_last` | ✓ | |
| `stt_url`, `stt_model`, `stt_key` — saved by a passed test on 06h1; set = a key is saved | | ✓ |
| `ai_url`, `ai_model`, `ai_key` — saved by a passed test on 06h2; empty URL / key = speech-to-text's; set = a model is saved (none → *AI key missing*) | | ✓ |
| `ai_plain` (Plain text, default 0), `ai_instr` (Note instructions, default 1) | | ✓ |

## 9. Network

- **Wi-Fi:** never blocks the screen. At boot and after a drop, a background scan picks the strongest saved network in range and joins it; retries every 30 s while none is found. Wrong password is told apart from "not found" by the disconnect reason (auth fail / 4-way handshake timeout). Hotspot (04j On) runs in AP+STA mode next to the home connection. Scanning while `softAP()` starts crashed the Wi-Fi driver on ESP32 core 3.3.12, so the scan runs just before the hotspot starts and the list is kept; a refresh while it runs is to be tested on the device. Networks with a sign-in page (hotels, cafés) are not supported.
- **Setup page:** served only on the Sticky's own WPA2 hotspot at `192.168.4.1` while 04j is On, never on the home network. Saved keys are shown masked and never sent back to the browser.
  Known limit: plain HTTP, but only between the phone and the Sticky over its password-protected hotspot.
- **City lookup:** `https://geocoding-api.open-meteo.com/v1/search?name={city}&count=5` → lat/lon/timezone (phone page suggestions; the Sticky itself uses count=1 for a typed name)
- **Weather:** `https://api.open-meteo.com/v1/forecast?latitude=…&longitude=…&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto`
- **Time:** NTP; UTC offset from `utc_offset_seconds` in each weather reply (follows DST within 30 min, no timezone table)
- **AI (Phase 2):** `POST {stt_url}/audio/transcriptions` (multipart WAV) and `POST {ai_url}/chat/completions`, `Authorization: Bearer {key}`. The 06h Save test sends 0.5 s of silence or a 16-token "Hi" chat; 401/403 → wrong key, 400/404/422 → check URL and model, no answer → couldn't reach
- **HTTPS:** certificates checked with the ESP32 core's built-in root CA bundle (`useBuiltinCACertBundle()`)
- **OTA:** `https://api.github.com/repos/{owner}/heysticky/releases/latest` → compare `tag_name` with the running version → download the release asset `heysticky-app.bin` over HTTPS → `HTTPUpdate` writes the inactive OTA slot. Each release publishes the app `.bin` as `heysticky-app.bin` (OTA) plus the four first-install files and `manifest.json`, all made by `tools/release.sh vX.Y.Z` into `release/vX.Y.Z/`. The tag is `vX.Y.Z` and must match `FW_VERSION` in `settings.h`. The repo is `OTA_REPO` in `ota.h`: `TaDuy2005/heysticky` (public, MIT). Rollback: the new image marks itself good at the end of `setup()`; a reset before that boots the old slot

## 10. Project layout

```
HeySticky/
  PLAN.md  README.md  LICENSE
  heysticky/
    heysticky.ino   app switching, main loop
    sketch.yaml     pinned core + libraries
    pins.h          pin map
    hw.h            touch, buttons, buzzer, SHT40, battery
    net.h           background Wi-Fi join, hotspot, phone setup page + /api, NTP, weather
    wifi_list.h     saved networks (host test: tools/wifi_test.cpp)
    ui.h            bars, rows, keyboard, focus drawing
    screens.h       all screens
    store.h         SD files + Preferences
    ota.h           Software Update: GitHub release check, HTTPUpdate install, rollback mark
    power.h         Sleep after (light sleep, minute clock wake, 30-min sync) and shutdown (UP + DOWN, battery empty)
    lang.h          English / Vietnamese: g_vi + TR(en, vi)
    userfont.h      user fonts from /fonts/<Name>/ on the card (04c)
    src/webfont.h   JetBrains Mono Regular + Bold as WOFF for the phone page (made by tools/make_webfont.py)
    mic.h           PDM mic: power, record into PSRAM, level for the waveform (Phase 2)
    ai_parse.h      prompt, multipart body, reply parsing (host test: tools/ai_test.cpp)
    ai.h            speech-to-text + sorting in a task, 06h connection test
    ai_ui.h         05 Listening · 05a Working · 05b Review · 05c Done · 05d Failed
  release/          exported .bin (git-ignored)
  tools/make_webfont.py  fonts/ -> heysticky/src/webfont.h (Latin + Vietnamese subset, ~48 KB each; needs fontTools)
  tools/build.sh    compile / upload / export for 32 MB flash (home folder stripped from the .bin)
  tools/release.sh  release/vX.Y.Z/: four flash files + manifest.json + heysticky-app.bin
```

## 11. Publishing (after Phase 1 is tested)

1. Public GitHub repo with source + MIT `LICENSE`.
2. Export merged `.bin` (32 MB flash, DIO, 80 MHz) and test-flash it on the device.
3. Submit via the **Upload firmware** form on the Sticky Playground Firmware page: name, category (Productivity), summary, version, MIT, real device photo, repo URL, `.bin` at `0x0`.

## 12. Decisions & open questions

- [x] Portrait 480×800, pure black & white
- [x] City is typed by the user (no IP geolocation)
- [x] Hold top button is reserved for AI; sleep is idle-only
- [x] AI providers are OpenAI-compatible and user-configurable (DeepSeek for chat; a separate speech-to-text provider, since DeepSeek has no audio API)
- [x] AI keys are saved only after the Sticky has tested them (06h3), and each service can be deleted (06h4) (2026-11-18)
- [ ] Device + microSD card needed on USB-C from TASK-02
- [ ] Deadline reminder beeps — Phase 3 unless pulled into Phase 1
- [x] SN: in eFuse BLOCK3 (user data), first 8 bytes little-endian, printed as 18 digits like the factory firmware's `%018llu` (this Sticky: 123456789012345678). eFuse is never touched by flashing
- [ ] MAC: read from efuse (`esptool read_mac`; `ESP.getEfuseMac()` in firmware) — no unknowns
- [ ] Certifications: not readable from the chip, and the factory firmware has no certification text either. About shows the Figma placeholder "FCC · CE" until the text on the device label is confirmed
- [x] User fonts use our own format, not Seeed's `.fpack`: plain VLW files per slot, made on the phone page (TASK-14)
- [x] Web setup is a row in Settings (below Configuration). Everything (name, city, Wi-Fi, integrations) is set on the phone setup page, reached only through the Sticky's hotspot (04j On → edit → Save → Off). Sleep after is a row below Configuration (04l, TASK-11)
- [x] Wi-Fi is set like iPhone Settings on the phone page (06b–06f), not on the Sticky: up to 5 saved networks, background auto-join, wrong-password alert, Forget. Hotspot gets a random password each time it is turned on (2026-11-17)
- [ ] No microSD card: needs a Figma frame. Interim (TASK-08): "No microSD card" + "Insert a FAT32 card, then tap Retry." + Retry button
- [ ] Later: tap inside the text to put the cursor there (edit mid-text: map tap → character, insert/delete at the cursor). Today the cursor is always at the end.
- [x] BQ27220 runs at 10× scale (factory config: design/full-charge capacity 7500 mAh for the 750 mAh cell), so its mA are divided by `GAUGE_SCALE` = 10 in `hw.h`. Its data memory is sealed (read-only check, 2026-11-17); we never write to the gauge. Charging stops by itself at full (100%, 0 mA, FC flag set)
- [ ] 16 px edge margin for the status bar, page counter and bottom buttons is a visual choice; confirm against the visible-pixel test on the real panel

## 13. Strings (EN / VI)

Firmware string table for TASK-14; the Figma "Tiếng Việt (VI)" section uses the same text. Short forms are for tight spots (app tiles, buttons, cards).

| Key | English | Tiếng Việt |
|---|---|---|
| hello | Hello, {name} | Xin chào, {name} |
| weekdays | Mon Tue Wed Thu Fri Sat Sun | T2 T3 T4 T5 T6 T7 CN |
| date | Monday, 16 November | Thứ Hai, 16 tháng 11 |
| month_year | November 2026 | Tháng 11 2026 |
| hi_lo | H 31°  L 25° | C 31°  T 25° |
| offline | Offline | Ngoại tuyến |
| indoor / humidity | Indoor / Humidity | Phòng / Độ ẩm |
| today_n | {n} tasks today | {n} việc hôm nay |
| today_none | No tasks today | Không có việc |
| app_todo | To-do | Việc cần làm (tile, button: Việc) |
| app_notes | Notes | Ghi chú |
| app_settings | Settings | Cài đặt |
| apps | Apps | Ứng dụng |
| left_n | {n} left | còn {n} |
| overdue | Overdue | Quá hạn |
| today / tomorrow | Today / Tomorrow | Hôm nay / Ngày mai |
| add_task / new_task / edit_task | Add task / New task / Edit task | Thêm việc / Việc mới / Sửa việc |
| new_note / edit_note | New note / Edit note | Ghi chú mới / Sửa ghi chú |
| title / placeholder | Title / Start typing… | Tiêu đề / Bắt đầu gõ… |
| deadline_opt | DEADLINE (OPTIONAL) | HẠN CHÓT (TUỲ CHỌN) |
| date / time / off | Date / Time / Off | Ngày / Giờ / Tắt |
| save / edit / delete / cancel / done | Save / Edit / Delete / Cancel / Done | Lưu / Sửa / Xoá / Huỷ / Xong |
| key_space | space | cách |
| del_task | Delete task? | Xoá việc này? |
| discard_task / discard_note / discard_changes / discard | Discard task? / Discard note? / Discard changes? / Discard | Bỏ việc này? / Bỏ ghi chú? / Bỏ thay đổi? / Bỏ |
| del_note / del_note_body | Delete note? / Note #{n} will be deleted from the SD card. | Xoá ghi chú? / Ghi chú #{n} sẽ bị xoá khỏi thẻ SD. |
| empty_todo | No tasks yet / Tap Add task to make one. | Chưa có việc nào / Chạm Thêm việc để tạo. |
| empty_notes | No notes yet / Tap New note, or hold ● to speak one. | Chưa có ghi chú / Chạm Ghi chú mới, hoặc giữ ● để nói. |
| auto_rotate / sound | Auto-rotate / Sound | Tự xoay / Âm thanh |
| lang_font / sys_lang / user_font | Language & Font / System Language / User Font | Ngôn ngữ & Phông / Ngôn ngữ hệ thống / Phông chữ |
| font_default | Default (built-in) | Mặc định (có sẵn) |
| sw_update / up_to_date / check | Software Update / You're up to date / Check for updates | Cập nhật phần mềm / Đã là bản mới nhất / Kiểm tra cập nhật |
| config / city / tz / refresh | Configuration / City / Timezone / Refresh interval | Cấu hình / Thành phố / Múi giờ / Làm mới |
| temp / c / f | Temperature / Celsius (°C) / Fahrenheit (°F) | Nhiệt độ / Độ C (°C) / Độ F (°F) |
| about / name / sw_ver / certs | About / Name / Software version / Certifications | Thông tin / Tên / Phiên bản / Chứng nhận |
| sd / free / inserted / mounted / path / space / files | SD card / {x} free / Inserted / Mounted / Path / Space / Files | Thẻ SD / Trống {x} / Đã lắp / Đã gắn / Đường dẫn / Dung lượng / Tệp |
| yes / on | Yes / On | Có / Bật |
| web_setup / password | Web setup / Password | Cài đặt qua web / Mật khẩu |
| scan_join / then_open / may_open | Scan to join / Then open / It may open by itself. | Quét để kết nối / Rồi mở / Trang có thể tự mở. |
| sd_files / no_files | SD Files / No files | Tệp trên thẻ SD / Không có tệp |
| (phone page) wifi / my_networks / other_networks / other | Wi-Fi / MY NETWORKS / OTHER NETWORKS / Other… | Wi-Fi / MẠNG CỦA TÔI / MẠNG KHÁC / Khác… |
| join_net / join_hint / joining / cant_join | Join “{ssid}” / Enter the password to join this Wi-Fi network. / Joining… / Couldn't join | Kết nối “{ssid}” / Nhập mật khẩu để kết nối mạng Wi-Fi này. / Đang kết nối… / Không kết nối được |
| wrong_pass / ok | Incorrect password / OK | Sai mật khẩu / OK |
| net_status / connected / saved / signal / security / ip | Status / Connected / Saved / Signal / Security / IP address | Trạng thái / Đã kết nối / Đã lưu / Tín hiệu / Bảo mật / Địa chỉ IP |
| signal levels | Excellent / Good / Weak | Rất tốt / Tốt / Yếu |
| forget_net / forget_q / forget_hint / forget | Forget This Network / Forget "{ssid}"? / Your Sticky will stop joining it. / Forget | Quên mạng này / Quên "{ssid}"? / Sticky sẽ không tự kết nối mạng này nữa. / Quên |
| ai_listening / ai_working | Listening… / Working on it… | Đang nghe… / Đang xử lý… |
| ai_steps | Heard you / Sorting it out… / Saving… | Đã nghe / Đang phân loại… / Đang lưu… |
| ai_review / ai_unsure / ai_again | Here's what I heard / Not sure. Save it as: / Record again? | Tôi đã nghe được / Chưa rõ. Lưu thành: / Ghi âm lại? |
| ai_added / ai_list | Added / TO-DOS LIST | Đã thêm / DANH SÁCH VIỆC |
| ai_fail / ai_no_net / retry / home | Couldn't do that / No internet connection / Try again / Home | Không làm được / Không có Internet / Thử lại / Trang chủ |
| ai_err_* | I didn't catch that · AI key missing — open Web setup · AI service error ({code}) | Tôi chưa nghe rõ · Thiếu khoá AI — mở Cài đặt qua web · Lỗi dịch vụ AI ({code}) |
