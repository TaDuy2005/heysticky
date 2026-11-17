# HeySticky

Personal assistant firmware for the **Seeed reTerminal Sticky** (ESP32-S3, 3.97" e-paper, portrait).
A home dashboard with clock, weather and indoor climate, to-dos with deadlines, notes on the microSD card,
a phone setup page, English and Vietnamese, user fonts, sleep with a live clock, and over-the-air updates.

## Features

- **Home:** "Hello, {name}", big clock and date, weather (Open-Meteo, no API key), indoor temperature and
  humidity, today's to-do count, app dock.
- **To-do:** add, tick, edit (long-tap), delete with confirmation, optional deadline (date and/or time).
  Stored as plain Markdown in `/todo.md` on the microSD card.
- **Notes:** create, view, edit, delete. One Markdown file per note in `/notes/`.
- **On-screen keyboard** with phone-like auto-capitals and double-space full stop.
- **Phone setup page:** turn on Web setup on the Sticky, scan the QR code to join its hotspot, open
  `192.168.4.1` to set your name, city and Wi-Fi (up to 5 saved networks, joined automatically).
- **Settings:** Sound (on/off, volume Low / Medium / High), Language (English / Tiếng Việt), User Font, Software Update, Configuration
  (city, timezone, °C/°F), Sleep after, Web setup, About (SN, MAC, version, SD card).
- **Power:** sleeps after 1–10 min idle (Home with a moon, clock still updates every minute, weather every
  30 min); shutdown with UP + DOWN; turns itself off when the battery is empty.
- **Software Update:** checks this repository's GitHub Releases and installs over Wi-Fi, with rollback if the
  new version fails to start.

## Controls

| Action | How |
|---|---|
| Home | Press the top button, or swipe up from the bottom edge |
| Back / forward | DOWN / UP, or swipe right / left |
| Tick a to-do | Tap it |
| Edit a to-do | Long-tap it |
| Wake from sleep | Press the top button |
| Turn off | Hold UP + DOWN together for 3 s |
| Turn on | Hold the top button for 2 s (a short tap does nothing) |

## First setup

1. Insert a microSD card (FAT32) and flash the firmware (below).
2. With no saved Wi-Fi the Sticky opens **Web setup** by itself: scan the QR code with your phone
   (or join `HeySticky-Setup` with the password shown), then open `192.168.4.1`.
3. Set **Profile** (name, city) and **Wi-Fi**. Turn Web setup off on the Sticky when done.

Wi-Fi passwords and settings are kept in the Sticky's flash only. They are never in the source or the `.bin`,
and the setup page is served only on the Sticky's own password-protected hotspot.

## microSD layout

Plain text, readable on any computer:

```
/todo.md                  - [ ] Call the dentist @due(2026-11-17 09:00)
                          - [x] Buy milk
/notes/012.md             # Title
                          Body text …
/fonts/<Name>/<slot>.vlw  user font, one file per size/style, added from the phone page (Fonts)
```

## Install

**Seeed Sticky Playground:** flash it in the browser from the Firmware page (once listed).

**From a GitHub Release:** download the files from the latest [release](https://github.com/TaDuy2005/heysticky/releases)
and write them with [esptool](https://github.com/espressif/esptool):

```bash
esptool --chip esp32s3 --baud 460800 write-flash --flash-mode dio --flash-freq 80m --flash-size 32MB \
  0x0 bootloader.bin 0x8000 partition-table.bin 0xe000 boot_app0.bin 0x10000 heysticky.bin
```

Later versions install from **Settings › Software Update** over Wi-Fi; the notes, to-dos and settings stay.

## Build from source

Needs [arduino-cli](https://arduino.github.io/arduino-cli/). The ESP32 core 3.3.12 and ArduinoJson are pinned in
`heysticky/sketch.yaml`. Seeed_GFX2 is not in the Arduino index, so clone it next to the sketch:

```bash
git clone https://github.com/Seeed-Studio/Seeed_GFX2 libs/Seeed_GFX2
git -C libs/Seeed_GFX2 checkout 002798b7b0adc7b3ada8cfd4db6d8a7ca11030f3
```

```bash
tools/build.sh                              # compile
tools/build.sh -u -p /dev/cu.usbmodemXXXX   # compile and upload
tools/release.sh v1.0.0                     # release files + manifest.json into release/v1.0.0/
```

Always build through `tools/build.sh`: it selects the 32 MB flash with two 12.5 MB OTA slots, which an
update can never change later. Host tests for the data formats:

```bash
for t in notes todo settings wifi; do c++ -std=c++17 -I heysticky tools/${t}_test.cpp -o /tmp/${t}_test && /tmp/${t}_test; done
```

Fonts and icons are generated: `python3 tools/make_fonts.py` (JetBrains Mono TTFs in `fonts/`) and
`python3 tools/make_icons.py` (PNGs in `tools/icons/`). `PLAN.md` is the full design and behaviour spec.

### Releasing an update

1. Raise `FW_VERSION` in `heysticky/settings.h`, commit and push.
2. `tools/release.sh vX.Y.Z`
3. Create GitHub Release `vX.Y.Z` with the files from `release/vX.Y.Z/`. The Sticky downloads the asset named
   `heysticky-app.bin`.

## Device test record

| | |
|---|---|
| Hardware | reTerminal Sticky (ESP32-S3R8, 8 MB PSRAM, 32 MB flash, 3.97" e-paper, GT911 touch) |
| Firmware | HeySticky v1.0.0, built with ESP32 Arduino core 3.3.12 |
| Date | 2026-11-17 |

| Test | Result |
|---|---|
| First boot without Wi-Fi opens Web setup; phone page saves name, city and Wi-Fi | Pass |
| Home: clock (NTP), weather, indoor temperature/humidity, today's to-dos | Pass |
| To-do add / tick / edit / deadline / delete, saved to `/todo.md` | Pass |
| Notes create / view / edit / delete, saved to `/notes/` | Pass |
| English / Vietnamese switch; user font from the phone page | Pass |
| Sleep after 2 min idle, clock updates each minute, top button wakes | Pass (on USB) |
| UP + DOWN shutdown; 2 s top-button hold turns on, a tap does not (USB and battery) | Pass |
| Software Update from GitHub Releases | See release notes |

## Credits and licences

- HeySticky: MIT, see [LICENSE](LICENSE).
- [Seeed_GFX2](https://github.com/Seeed-Studio/Seeed_GFX2) display library (MIT), Seeed Studio.
- [ArduinoJson](https://arduinojson.org/) (MIT), Benoît Blanchon.
- [JetBrains Mono](https://www.jetbrains.com/lp/mono/) font (SIL Open Font License 1.1, `fonts/OFL.txt`).
- Weather and city search: [Open-Meteo](https://open-meteo.com/) (CC BY 4.0 data).
- Pin map from Seeed's open-source sticky-2048 firmware.
