# HeySticky — Project Plan

Personal assistant firmware for the **Seeed reTerminal Sticky** (ESP32-S3, 3.97" e-paper).
Home dashboard, to-dos with deadlines, notes on microSD, a phone setup page, and (V2) hold-to-talk AI.

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
- Board: `esp32:esp32:XIAO_ESP32S3:PSRAM=opi,CDCOnBoot=cdc`. `cdc` means USB CDC *Disabled*, which is what we want: the USB-C port goes through a USB-UART bridge ("USB Single Serial"), so `Serial` must be UART0. The board menu only offers 8 MB flash; the 32 MB partition table comes with TASK-13
- Seeed_GFX2 is not in the Arduino index: cloned into `libs/Seeed_GFX2` (git-ignored) at a pinned commit, referenced from `sketch.yaml`
- Libraries: **Seeed_GFX2**, **ArduinoJson**, a small QR-code library; core built-ins `WiFi`, `WebServer`, `DNSServer`, `HTTPClient`, `Preferences`, `SD`
- Sensors/touch over raw I2C (no extra libraries)
- Fonts: JetBrains Mono TTFs in `fonts/` (OFL) -> `tools/make_fonts.py` (Pillow) -> VLW headers in `heysticky/src/fonts/`, pre-thresholded to pure black/white because the 1-bit panel maps any non-black pixel to white. Spaces are drawn as U+00A0 (the library guesses U+0020's width). Text is placed by Figma text box: top = box y + (box h - ascent - descent) / 2
- Icons: exported from Figma at 1x into `tools/icons/` -> `tools/make_icons.py` -> 1-bit bitmaps in `heysticky/src/icons.h` (alpha threshold 64 keeps ~1.5 px strokes unbroken)
- Refresh: full on screen change and at boot (after a white clear); partial for clock, weather, indoor, status bar; a full refresh every 30 partials (20 while typing)
- Measured on this unit: a partial refresh takes ~860 ms regardless of area. Touch is read by a FreeRTOS task on core 0 into a queue, so taps made during a refresh are applied in the next one; typing is silent (a beep per key cannot keep pace)
- Services: **Open-Meteo** (weather + city lookup + timezone, no key), **NTP** (time)
- V2: any **OpenAI-compatible** API — speech-to-text (`/audio/transcriptions`) and chat (`/chat/completions`, e.g. DeepSeek)

## 3. Interaction model

- **Touch** everything. Swipe left/right between pages (Home 1/2 ⇄ Apps 2/2, To-do and Notes pages, a note's pages; right on To-do / Notes page 1 → Apps, on a note's page 1 → Notes).
- **DOWN = back**, same as swiping left-to-right; **UP = forward**, same as swiping right-to-left. They only move between pages; in the note editor they hide the keyboard if it is open, otherwise scroll the body (DOWN = earlier, UP = later); elsewhere without pages they do nothing (they are never Cancel).
- **Press top button = Home**, from any screen. **Swipe up from the bottom edge = Home** too (start in the bottom 15 px, y ≥ 785; keys ignore touches that start there).
- **Hold top button = talk to AI** (V2). Sleep is by idle timer, not a button hold.
- **Long-tap a to-do row = edit.** Tap = tick.
- **Note body box scrolls**: swipe up/down inside it moves a box-full (minus one line); a scrollbar shows on the right edge when the text doesn't fit. Typing jumps back to the end.
- Partial refresh for small changes (typing, ticking); full refresh when changing screens.
- No bottom hint bar: screens don't spell out what the buttons do.

## 4. Screens (Figma numbering)

| Row | Screens |
|---|---|
| Home & system | 01 Home · 01a Apps · 01b Sleep (Home with a moon in the status bar) · 01c Home (no tasks today) · 01d Sleep (no tasks today) |
| To-do | 02 To-do list · 02a New task (keyboard) · 02b New task (no keyboard) · 02c Date · 02d Edit task (keyboard) · 02e Edit task (no keyboard) · 02f Delete task (confirm) · 02g To-do (empty) · 02h Time |
| Notes | 03 Notes list · 03a New note (keyboard) · 03b New note (no keyboard) · 03c Note view · 03d Edit note (keyboard) · 03e Edit note (no keyboard) · 03f Delete note (confirm) · 03g Notes (empty) |
| Settings & setup | 04 Settings · 04a Language & Font · 04b System Language · 04c User Font · 04d Software Update · 04e Configuration · 04f Location · 04g Temperature · 04h About · 04i SD Card · 04j Web setup · 06 Phone setup page |

**Layout & type (all Sticky screens; not the phone setup page):**
- Font: **JetBrains Mono** (Regular, Bold, ExtraBold). Minimum text size **25 px**; the status bar and the page counter are the only exceptions, at **20 px**.
- Content area: 40 px left/right margin (x 40–440); dividers span that width.
- Status bar (top, 40 px tall) sits **16 px** from the screen edges (x 16–464): "HeySticky" left; Wi-Fi (moon when asleep) + bolt (while plugged in, Figma StatusBar Mode=Charging) + battery % right.
- Page counter (`1/2`, no spaces): centred, 16 px above the bottom edge, **only on screens with more than one page** (Home/Apps, To-do, Notes, Note view). No hint bar.
- Bottom buttons sit **16 px** above the bottom edge; on screens with a page counter they sit 12 px above it (bottom at y 746).
- To-do rows: checkbox, then the task title with its due date on the line below. Notes list: title on one line, cut with "…".
- Task deadline: under DEADLINE (OPTIONAL) the editor has two rows, `Date Off ›` and `Time Off ›` (values `17/11/26`, `09:00` once set). Date (02c) = toggle + calendar + Done; Time (02h) = toggle + HH:MM fields + number pad (Done on the pad). Cancel undoes that screen. Date only = date-only `@due`; Time only = today at that time; both off = no deadline.
- Editing: every keyboard's last key is **↵** (no "Done" text; the 02h time keypad too). In one-line fields (task, note title) ↵ only hides the keyboard; in the note body ↵ = new line and UP/DOWN hide the keyboard. Hiding the keyboard is a full refresh (a partial one leaves its ghost). Tap a field to bring the keyboard back. The cursor is always at the end of the text. Shift turns on by itself at the start of a field or line and after ". " / "! " / "? " (one letter, like a phone). Also like a phone: two spaces after a word make ". ", and a space on the 123 page goes back to letters. Empty fields show an Italic placeholder (*Task*, *Title*, body hint); a focused field shows it with the cursor after it until tapped or typed in. Edit task / Edit note have **Save** and **Delete** buttons; New task / New note have **Save**. Delete always opens a confirmation (Cancel / Delete).

**Settings (08):** Auto-rotate (toggle) · Sound (toggle) · Language & Font › · Software Update › · Configuration › · About ›. Same layout as Seeed's official Sticky firmware.
- **Language & Font:** System Language › (English / Tiếng Việt checkboxes) · User Font › (built-in or any `.fpack` in `/fonts/` on the microSD, exported with Seeed Font Tool, same as the official firmware).
- **Software Update:** current version, "up to date" / "update available", **Check for updates** button (OTA, see §9).
- **Configuration:** City › (Location search) · Timezone (follows the city, read-only) · Refresh interval (30 min, fixed) · Temperature › (°C / °F checkboxes).
- **About:** Name, SN, MAC, Software version, Certifications ›, SD card › (Inserted, Mounted, Path, Space, Files ›).
- **Auto-rotate:** setting is stored; no screen supports rotation yet (needs the IMU).
| AI voice (V2) | 05 Listening · 05a Working · 05b Review (only when unsure) · 05c Done · 05d Failed |

**Home:** "Hello, {name}", big clock + date, weather card (condition, temp, high/low, city), indoor temp + humidity (SHT40), Today card (number of to-dos due today, e.g. "3 tasks today"; "No tasks today" when none; tap → To-do), app dock (To-do, Notes, Home*, Reader*, Settings). *V3.

## 5. Versions

### V1 — core (offline-capable, Wi-Fi for time & weather)
Wi-Fi + phone setup page, Home dashboard, To-do with optional deadlines, Notes, Settings/About/Location, power management, release `.bin`.

### V2 — AI voice
Hold top button → record → speech-to-text → **Working** → **Done**. Only when the AI can't tell whether it's a to-do or a note does it show **Review** (transcript; choose **To-do** or **Note**, or **Record again**), then Done.
- **To-do:** AI splits the sentence into items and extracts deadlines ("tomorrow at 9" → `@due`).
- **Note:** AI writes the note following the user's **Note instructions** (outline/template/rules, typed or uploaded as .txt on the phone setup page, max 2,000 characters); "Plain (no AI)" saves the transcript as-is.
- Done / Failed screens with the reason, plus success/fail beeps (if Sound is on).
- Listening wave: while recording, partial-refresh only the wave strip (260×80) about 3×/s, bar heights from the mic level; bars go flat when silent. Full refresh when leaving Listening. Figma: `Waveform` component, Step 1→4 looping every 0.35 s.

### V3 — later (each its own feature branch)
Notion daily sync · Home Assistant dashboard · Reader · deadline reminder beeps (wake from sleep at due time) · Tailscale (experimental, community ESP-IDF libs only).

## 6. V1 requirements

- [x] REQ-01 First boot with no Wi-Fi → hotspot `HeySticky-Setup`, Web setup screen shows `192.168.4.1` + QR
- [x] REQ-02 Phone setup page saves Wi-Fi, name, city, units — stored on the device only
- [x] REQ-03 Home shows greeting, time/date, weather, indoor temp/humidity, Today count, dock
- [ ] REQ-04 Time from NTP; weather every 30 min; no Wi-Fi → weather card says "Offline", everything else works
- [x] REQ-05 To-do: add, tick, edit (long-tap), save, delete with confirmation, optional deadline (Date and Time, each on/off); overdue first, then by due date
- [x] REQ-06 Notes: create, list, view, edit, save, delete with confirmation
- [x] REQ-07 On-screen keyboard with partial refresh
- [ ] REQ-08 Settings: Auto-rotate, Sound, Language & Font, Software Update, Configuration (city search, auto timezone, 30-min refresh, °C/°F), About
- [ ] REQ-09 Touch works on every screen; DOWN/UP page back/forward, top button and bottom-edge swipe go Home (§3)
- [ ] REQ-10 Idle → sleep; wake each minute to redraw the clock; top button wakes fully
- [ ] REQ-11 No secrets in source or in the `.bin`
- [ ] REQ-12 Missing SD card → "No microSD card" screen with retry (built in TASK-08, interim layout; not yet tested with the card removed)
- [ ] REQ-13 Release build is one merged `.bin` written at `0x0`
- [ ] REQ-14 OTA: on Wi-Fi connect and on "Check for updates", compare with the latest GitHub Release; download, verify and install the app `.bin` into the spare OTA slot; reboot; roll back if the new image fails to start. SD data untouched
- [ ] REQ-15 Language English/Vietnamese; user fonts loaded from `/fonts/*.fpack`; built-in font covers Vietnamese diacritics
- [ ] REQ-16 About shows name, SN, MAC, software version, certifications and live SD card status (inserted, mounted, path, free space, file count)

## 7. V1 task list

- [x] TASK-01 Pin core + libraries in `sketch.yaml`; compile Seeed's Sticky HelloWorld
- [x] TASK-02 Flash HelloWorld; confirm on the real screen (needs device on USB-C)
- [x] TASK-03 `hw.h`: touch, buttons, buzzer, SHT40, battery — verified over serial
- [x] TASK-04 `net.h`: settings in flash, Wi-Fi connect, hotspot fallback, web setup page
- [x] TASK-05 Clock (NTP + timezone) and weather (Open-Meteo)
- [x] TASK-06 Home screen
- [x] TASK-07 On-screen keyboard
- [x] TASK-08 To-do: list, add, tick, edit, delete, deadlines (Date + Time screens), `/todo.md` on microSD
- [x] TASK-09 Notes: list, view, new, edit, delete
- [ ] TASK-10 Settings, Configuration, Temperature, About, Web setup, Location screens. Sound toggle defaults **off**: wire it to `g_sound` in `hw.h` (all beeps are muted until then)
- [ ] TASK-13 OTA: two-slot OTA partition table, version check against GitHub Releases, `HTTPUpdate` install, rollback, Software Update screen
- [ ] TASK-14 Language & Font: string table (EN/VI), `/fonts/*.fpack` loader, font picker
- [ ] TASK-11 Power: idle sleep, minute clock wake, button wake
- [ ] TASK-12 README (controls, SD layout, device test record), LICENSE (MIT), export merged `.bin`

## 8. Data formats

**microSD** (plain text, readable on any computer):

```
/todo.md            - [ ] Call the dentist @due(2026-11-17 09:00)
                    - [ ] Buy milk
                    - [x] Flash HelloWorld
/notes/012.md       # Idea: sticky habit grid
                    Show a 7×5 grid …
/ai/note-instructions.txt  (V2) how the AI writes notes, set on the phone setup page
/fonts/*.fpack      user fonts (Seeed Font Tool export)
```

**Device flash (`Preferences`)** — never in source, never in the `.bin`:

| Key | V1 | V2 |
|---|---|---|
| `wifi_ssid`, `wifi_pass` | ✓ | |
| `name`, `city`, `lat`, `lon`, `tz`, `units`, `geo_city` (city that lat/lon belong to) | ✓ | |
| `sound`, `sleep_min` | ✓ | |
| `rotate`, `lang`, `font`, `ota_last` | ✓ | |
| `stt_url`, `stt_model`, `stt_key` | | ✓ |
| `ai_url`, `ai_model`, `ai_key` | | ✓ |

## 9. Network

- **Wi-Fi:** no network scan in hotspot mode (scanning around `softAP()` crashes the Wi-Fi driver on ESP32 core 3.3.12); the user types the network name.
- **Setup page:** served by the Sticky on its IP (or `192.168.4.1` in hotspot mode). Saved keys are shown masked and never sent back to the browser.
  Known limit: the page is plain HTTP on the local network.
- **City lookup:** `https://geocoding-api.open-meteo.com/v1/search?name={city}&count=5` → lat/lon/timezone
- **Weather:** `https://api.open-meteo.com/v1/forecast?latitude=…&longitude=…&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto`
- **Time:** NTP; UTC offset from `utc_offset_seconds` in each weather reply (follows DST within 30 min, no timezone table)
- **HTTPS:** certificates checked with the ESP32 core's built-in root CA bundle (`useBuiltinCACertBundle()`)
- **OTA:** `https://api.github.com/repos/{owner}/heysticky/releases/latest` → compare `tag_name` with the running version → download the release asset `heysticky-app.bin` over HTTPS → `HTTPUpdate` writes the inactive OTA slot. Each release publishes both the app `.bin` (OTA) and the merged `.bin` (first install at `0x0`)

## 10. Project layout

```
HeySticky/
  PLAN.md  README.md  LICENSE
  heysticky/
    heysticky.ino   app switching, main loop
    sketch.yaml     pinned core + libraries
    pins.h          pin map
    hw.h            touch, buttons, buzzer, SHT40, battery
    net.h           Wi-Fi, hotspot, setup page, NTP, weather
    ui.h            bars, rows, keyboard, focus drawing
    screens.h       all screens
    store.h         SD files + Preferences
  release/          exported .bin (git-ignored)
```

## 11. Publishing (after V1 is tested)

1. Public GitHub repo with source + MIT `LICENSE`.
2. Export merged `.bin` (32 MB flash, DIO, 80 MHz) and test-flash it on the device.
3. Submit via the **Upload firmware** form on the Sticky Playground Firmware page: name, category (Productivity), summary, version, MIT, real device photo, repo URL, `.bin` at `0x0`.

## 12. Decisions & open questions

- [x] Portrait 480×800, pure black & white
- [x] City is typed by the user (no IP geolocation)
- [x] Hold top button is reserved for AI; sleep is idle-only
- [x] AI providers are OpenAI-compatible and user-configurable (DeepSeek for chat; a separate speech-to-text provider, since DeepSeek has no audio API)
- [ ] Device + microSD card needed on USB-C from TASK-02
- [ ] Deadline reminder beeps — V3 unless pulled into V1
- [ ] SN: where the factory firmware stores it is unknown (efuse user block? an NVS/factory partition?). Read the device's partition table before the first flash; if our merged `.bin` would overwrite it, back it up and keep that partition
- [ ] MAC: read from efuse (`esptool read_mac`; `ESP.getEfuseMac()` in firmware) — no unknowns
- [ ] Certifications: not readable from the chip; show static text copied from the device label / Seeed's regulatory docs
- [ ] `.fpack` format is Seeed's; confirm it is documented or use our own font format
- [ ] Wi-Fi, Web setup, Your name and Sleep after are no longer in Settings — decide where they live (phone setup page only?)
  - Interim (TASK-04): Wi-Fi and Your name are set only on the phone setup page.
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
| del_note / del_note_body | Delete note? / Note #{n} will be deleted from the SD card. | Xoá ghi chú? / Ghi chú #{n} sẽ bị xoá khỏi thẻ SD. |
| empty_todo | No tasks yet / Tap Add task to make one. | Chưa có việc nào / Chạm Thêm việc để tạo. |
| empty_notes | No notes yet / Tap New note, or hold ● to speak one. | Chưa có ghi chú / Chạm Ghi chú mới, hoặc giữ ● để nói. |
| auto_rotate / sound | Auto-rotate / Sound | Tự xoay / Âm thanh |
| lang_font / sys_lang / user_font | Language & Font / System Language / User Font | Ngôn ngữ & Phông / Ngôn ngữ hệ thống / Phông chữ |
| font_default | Default (built-in) | Mặc định (có sẵn) |
| sw_update / up_to_date / check | Software Update / You're up to date / Check for updates | Cập nhật phần mềm / Đã là bản mới nhất / Kiểm tra cập nhật |
| config / city / tz / refresh | Configuration / City / Timezone / Refresh interval | Cấu hình / Thành phố / Múi giờ / Làm mới |
| temp / c / f | Temperature / Celsius (°C) / Fahrenheit (°F) | Nhiệt độ / Độ C (°C) / Độ F (°F) |
| location / now | Location / Now: {city} | Vị trí / Hiện tại: {city} |
| about / name / sw_ver / certs | About / Name / Software version / Certifications | Thông tin / Tên / Phiên bản / Chứng nhận |
| sd / free / inserted / mounted / path / space / files | SD card / {x} free / Inserted / Mounted / Path / Space / Files | Thẻ SD / Trống {x} / Đã lắp / Đã gắn / Đường dẫn / Dung lượng / Tệp |
| yes / on | Yes / On | Có / Bật |
| web_setup / open_phone / same_wifi / stop_web | Web setup / Open this on your phone / Use the same Wi-Fi. / Stop web setup | Cài đặt qua web / Mở trên điện thoại / Dùng cùng mạng Wi-Fi. / Dừng cài đặt web |
| hotspot | No Wi-Fi yet? Join the hotspot "HeySticky-Setup", then open 192.168.4.1 | Chưa có Wi-Fi? Kết nối điểm phát "HeySticky-Setup", rồi mở 192.168.4.1 |
| ai_listening / ai_working | Listening… / Working on it… | Đang nghe… / Đang xử lý… |
| ai_steps | Heard you / Sorting it out… / Saving… | Đã nghe / Đang phân loại… / Đang lưu… |
| ai_review / ai_unsure / ai_again | Here's what I heard / Not sure. Save it as: / Record again? | Tôi đã nghe được / Chưa rõ. Lưu thành: / Ghi âm lại? |
| ai_added / ai_list | Added / TO-DOS LIST | Đã thêm / DANH SÁCH VIỆC |
| ai_fail / ai_no_net / retry / home | Couldn't do that / No internet connection / Try again / Home | Không làm được / Không có Internet / Thử lại / Trang chủ |
| ai_err_* | I didn't catch that · AI key missing — open Web setup · AI service error ({code}) | Tôi chưa nghe rõ · Thiếu khoá AI — mở Cài đặt qua web · Lỗi dịch vụ AI ({code}) |
