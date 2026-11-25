# HeySticky — Roadmap

What is planned after v1.2.0. `PLAN.md` holds the full spec for each item once it is agreed; this page is the
short list. Updated 2026-11-25.

## Direction

The e-ink screen is a **dashboard**: it shows data and is read at a glance. Typing on it is out (the panel is too
slow for it). Input comes by **voice** (hold the top button) and from the **phone**: the setup page today, a native
iPhone app over Bluetooth later. Every screen is designed in Figma first, in English and Vietnamese.

## Released

- **v1.0** — Home dashboard (clock, weather, indoor climate, today's to-dos), To-do, Notes, Settings, phone setup
  page, sleep with a live clock, English / Vietnamese, user fonts, over-the-air updates.
- **v1.1** — AI voice: hold the top button and talk; the Sticky saves to-dos (with deadlines) or a note.
- **v1.2** — Phone To-do and Notes pages; on-screen keyboard removed; RTC clock that keeps time without Wi-Fi;
  created date and time on every item; 12-hour AM / PM times; hourly weather.

## Next — v1.3.0 Poster (`feature/poster`)

One task shown big, as a poster, until you take it down.

- [ ] **Display mode** switch on the task screen (02e) and the phone task form. One task at a time: turning it on
      for another task turns the first off. Only turning it off or deleting the task removes the poster; ticking
      it does not.
- [ ] **Poster page** as page 1 (1 poster · 2 Home · 3 Apps), no loop. Status bar kept, no Done button. Home,
      the bottom-edge swipe, waking and sleeping still land on Home.
- [ ] **Phone poster editor:** upload a picture per task; drag / pinch it in the 480 × 800 frame; drag the text
      box (S / M / L / XL, white backing) with the due label under it; live black-and-white **Preview**. The
      browser dithers the picture; it is saved on the card next to the task.
- [ ] **Push:** turns Display mode on and makes the Sticky jump to the poster at once (after a running recording).
- [ ] **Poster deep sleep:** asleep on the poster, the Sticky stays on it (moon) in deep sleep; a short top press
      wakes it to Home; an hourly timer wake (no Wi-Fi) updates the battery and checks for an empty battery.

## Then — editable Home widgets

- [ ] Choose and arrange the Home cards, built together with the first integration widget below, so the widget
      system is shaped by a real one.
- [ ] Widget ideas: smart-home light, Bambu Lab print progress, stocks.

## Integrations (each on its own branch)

- [ ] **Notion daily sync** — to-dos and notes; voice recordings handed to the phone over Bluetooth, then deleted
      from the card.
- [ ] **Home Assistant** — a dashboard of chosen entities; the Home tile in Apps (now "later").
- [ ] **Reader** — built on `feature/reader`, waiting for the device test: EPUB (with pictures), TXT and MD from
      `/books/` on the card, library with the last book first, place kept per book, text size S / M, Contents,
      upload and delete on the phone page. Layout by CrossPoint Reader's engine.
  - [ ] Later: **PDF** (converted to text on the phone page, pdf.js), Markdown `#` headings as chapters in
        Contents, books in sub-folders of `/books/`.
- [ ] **Deadline reminders** — beep at a task's due time, waking from sleep for it.
- [ ] **Remote access (Tailscale)** — experimental; only with community ESP-IDF libraries.

The phone page already has hidden rows for Notion, Home Assistant and Remote access (06i–06k).

## iPhone app over Bluetooth

- [ ] Native iPhone app (Safari has no Web Bluetooth) talking to the Sticky over BLE 5, next to Wi-Fi, without
      the hotspot: to-dos, notes and fonts.
- [ ] The app converts TTF / OTF into the Sticky's 13 font slots (about 1.1 MB a font, roughly 20–40 s to send).
- [ ] Chrome on Android / desktop could reuse the phone page's font converter through Web Bluetooth.
- [ ] Cost: a few hundred KB of flash for the BLE stack.

## Power

- [ ] Measure one night on battery with hourly weather and the RTC clock (v1.2).
- [ ] Then decide on **hourly deep sleep on Home** (clock updated hourly instead of every minute) for longer
      battery life.

## Small open items

- [x] **Seeed registry entry** — HeySticky 1.2.0 submitted as
      [registry PR #152](https://github.com/Seeed-Projects/reterminal-sticky-playground-registry/pull/152)
      (2026-11-21), device-tested. Waiting for Seeed's review; each new release needs a small PR there to update
      the Playground's browser flasher.
- [ ] **Certifications** in About — replace the "FCC · CE" placeholder with the text on the device label.
- [ ] **16 px edge margin** (status bar, page counter, bottom buttons) — check against the visible pixels on the
      real panel.
- [ ] **Auto-rotate** — the setting is stored; rotation needs the IMU.

## Paused

- **Repeat tasks** and other to-do extras (2026-11-20).
- **Raise to wake** (2026-11-25), under consideration and still in development on `feature/raise-to-wake`.
  Picking it up and tilting it 30° wakes it, and pushes or knocks don't, but in a trouser pocket it kept
  waking: the IMU alone can't tell when someone is looking at the screen (no proximity or light sensor).
