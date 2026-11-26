# HeySticky Roadmap

What comes after **v1.2.1**. Last updated 2026-11-26.

HeySticky turns the reTerminal Sticky into an e-ink **dashboard**: it shows your day at a glance and is not typed
on, because the panel is too slow for a keyboard. Input comes by **voice** (hold the top button) and from the
**phone**: the setup page today, a native iPhone app over Bluetooth later. Every screen is designed in Figma first,
in English and Vietnamese, before it is built.

**Status:** **Next** is being built now · **In development** is built on a branch and being tested · **Planned**
is agreed but not started · **Considering** is an idea, not a promise.

## At a glance

| Feature | Status |
| --- | --- |
| [Poster](#poster) | Next |
| [Reader](#reader) | In development |
| [Home widgets](#home-widgets) | Planned |
| [Home Assistant](#home-assistant) | Planned |
| [Notion daily sync](#notion-daily-sync) | Planned |
| [Remote access](#remote-access) | Planned (experimental) |
| [iPhone app over Bluetooth](#iphone-app-over-bluetooth) | Planned |
| [Considering features](#considering-features) | Considering |

## Released

- **v1.0** — dashboard, To-do, Notes, phone setup page, updates over Wi-Fi.
- **v1.1** — AI voice: speak a to-do or a note.
- **v1.2** — phone To-do and Notes pages, RTC clock, 12-hour times.
- **v1.2.1** — Report a bug (GitHub or email), Web setup on home Wi-Fi.

---

## Poster

**Status:** Next

**Why.** Some tasks matter more than a line in a list. A poster keeps one of them in front of you, big, with your
own picture, until you take it down.

**Tasks**

- [ ] **Display mode** switch on the task screen (02e) and the phone task form. One poster at a time: turning it
      on for another task turns the first off. Only turning it off or deleting the task removes the poster;
      ticking the task does not.
- [ ] **Poster page** as page 1 (1 Poster · 2 Home · 3 Apps), without looping. Status bar kept, no Done button.
      Home, the bottom-edge swipe, waking and sleeping still land on Home.
- [ ] **Phone poster editor:** upload a picture per task, drag and pinch it in the 480 × 800 frame, drag the text
      box (S / M / L / XL, white backing) with the due label under it, and a live black-and-white **Preview**. The
      browser dithers the picture; it is saved on the card next to the task.
- [ ] **Push** turns Display mode on and makes the Sticky jump to the poster at once (after a running recording).
- [ ] **Poster deep sleep:** asleep on the poster, the Sticky stays on it (with a moon) in deep sleep. A short top
      press wakes it to Home; an hourly timer wake (no Wi-Fi) updates the battery and checks for an empty battery.

## Reader

**Status:** In development

**Why.** A 480 × 800 e-ink screen is good for reading, and the card has room for a whole library.

**What is built.** EPUB (with pictures), TXT and Markdown from `/books/` on the card. The library puts the book
you were reading first; each book opens where you left it, also after sleep or a restart. Tap or swipe to turn the
page; the middle opens the reader menu. Books are uploaded and deleted on the phone page (06s). The reader menu also sets
the text size and the font: Noto Sans built in, or CrossPoint `.cpfont` fonts uploaded on the phone page. Layout
comes from the engine of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), built into
the firmware. Two rounds of device tests are done and fixed.

**Tasks**

- [x] Bring in the latest release and merge the phone page changes (Report a bug and the Books row both changed
      it).
- [ ] Device test on top of v1.2.1, then release.
- [ ] Later: **PDF** (converted to text on the phone page with pdf.js) and books in sub-folders of `/books/`.

## Home widgets

**Status:** Planned

**Why.** Home is the screen you see most. You should choose what is on it.

**Tasks**

- [ ] Choose and arrange the Home cards. Built together with the first integration widget, so the widget system is
      shaped by a real one rather than guessed.
- [ ] **Deadline reminders:** beep at a task's due time, waking from sleep for it.

**Widget ideas:** smart-home light, Bambu Lab print progress, stocks.

## Home Assistant

**Status:** Planned

**Why.** The Sticky sits on a desk or a wall, where a glance at the house is useful.

**Tasks**

- [ ] A dashboard of chosen Home Assistant entities, opened from the Home tile in Apps (now marked "later").
- [ ] Set up on the phone page; its row (06j) is already designed and hidden.
- [ ] Offer entities as Home widgets once [Home widgets](#home-widgets) exist.

## Notion daily sync

**Status:** Planned

**Why.** Many people keep their to-dos and notes in Notion; the Sticky should show the same list.

**Tasks**

- [ ] Daily two-way sync of to-dos and notes; its phone page row (06i) is already designed and hidden.
- [ ] Voice recordings handed to the phone over Bluetooth, then deleted from the card (needs the
      [iPhone app](#iphone-app-over-bluetooth)).

## Remote access

**Status:** Planned (experimental)

**Why.** Reach the phone page away from home, without opening ports on the router.

**Tasks**

- [ ] Tailscale on the ESP32-S3, possible only with community ESP-IDF libraries. Prove it builds, fits in flash
      and stays stable before any screen work.
- [ ] Set up on the phone page; its row (06k) is already designed and hidden.

**Open:** memory and battery cost of keeping the tunnel up.

## iPhone app over Bluetooth

**Status:** Planned

**Why.** Safari has no Web Bluetooth, and joining the Sticky's hotspot every time is slow. A native app talks to
the Sticky directly, next to Wi-Fi.

**Tasks**

- [ ] Native iPhone app talking to the Sticky over BLE 5: to-dos, notes and fonts, without the hotspot.
- [ ] Convert TTF / OTF in the app into the Sticky's 13 font slots (about 1.1 MB a font, roughly 20–40 s to send).
- [ ] Chrome on Android and desktop could reuse the phone page's font converter through Web Bluetooth.

**Cost:** a few hundred KB of flash for the BLE stack.

---

## Considering features

Ideas being weighed. None of them is promised.

- **Repeat tasks** (2026-11-20) and other to-do extras.
- **My reports** (2026-11-26), taken out of Report a bug for now. The Sticky (04u) and the phone page (06v) listed
  each report and followed its GitHub issue through GitHub's issue search: Waiting, Open, Fixed, Won't fix. A
  report not on GitHub within an hour (cancelled, never sent) dropped off; email reports can't be seen, so they
  weren't listed. *Open:* email reports could be followed if each daily summary issue names their `HS-XXXX` IDs.
  The working code is in commit `519b05c`; the Figma frames are kept as
  "Considering · 04u My reports".
- **Raise to wake** (2026-11-25), still in development. Picking the Sticky up and
  tilting it 30° wakes it, and pushes or knocks don't, but in a trouser pocket it kept waking: the IMU alone can't
  tell when someone is looking at the screen (there is no proximity or light sensor).
- **NFC module.** A 3D-printed case with an NFC reader that sticks to the back of the Sticky with magnets and sends
  each tag tap over Bluetooth (its own small BLE chip and battery). The magnets stay away from the NFC coil, with a
  ferrite sheet behind it. *Open:* what a tap does (tick a to-do, show a poster, log a check-in…).
- **Voice stand.** A 3D-printed, USB-powered stand with a speaker and microphone, like a smart speaker: it listens
  for a wake word, runs the AI and speaks the reply while the Sticky shows it, and charges the Sticky. Needs a
  mute switch and a listening light. Shares the BLE link with the iPhone app.

## Housekeeping

Smaller items that don't need a release of their own.

- [ ] **Battery:** measure one night on battery with hourly weather and the RTC clock, then decide on **hourly
      deep sleep on Home** (clock updated hourly instead of every minute) for longer battery life.
- [ ] **Auto-rotate:** the setting is stored; rotation needs the IMU.
