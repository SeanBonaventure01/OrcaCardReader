# ORCA Reader Costume — Implementation Plan

Companion to [ARCHITECTURE.md](ARCHITECTURE.md). Last updated **Sat 2026-10-17**. Halloween is
**Sat 2026-10-31**: 14 days left.

Each phase has a **demo checkpoint** (something you can watch working) and a **test gate**
(what has to pass before moving on). Testing details are in ARCHITECTURE §9.

---

## Status at a glance

| Area | State | Notes |
|---|---|---|
| Repo, host harness, `make check` | ✅ Done | Portability guard (`tools/check_portable.sh`), 250 host tests, 64 Vitest tests passing (2026-10-17) |
| Board bring-up | ✅ Done | ESP32-S3 N4R8 (4 MB flash, 8 MB PSRAM) confirmed. Panel verified at 15 MHz pclk. Factory image backed up. ESP-IDF v5.5.5. |
| Portable core (`game_core`, `api`, `config`, `orca_hal`) | ✅ Done | Engine, EventScheduler, snapshot save/restore, shared API with contract fixtures |
| `orca-sim` | ✅ Done | 10 golden scenarios, REPL, HTTP/WS server (`make sim`, `make repl`, `make scenarios`) |
| Phone UI | ✅ Done | Preact + Vite + TS. Live, Games, Players, Events, Settings, Board, Login. Playwright E2E (operator and games specs). |
| Costume firmware (Phase 3) | ✅ Done with a **simulated reader** | game_task, LVGL ORCA screens, Wi-Fi AP + captive DNS + mDNS, WebSocket, LittleFS snapshots, serial console, HIL smoke test with webcam |
| Games | ✅ 6 of 8 | Fare Paid, Rush Hour, Team Lines, Last Stop, Fare Inspector, Lottery. Not yet built: Route Collector, Golden Ticket. |
| PN532 protocol + driver (`nfc_core`) | ✅ Host-tested | Frames, firmware version, SAM config, polling, NTAG read/write, card payload codec, tap debouncer. Tested against datasheet frames only. |
| **PN532 I²C transport + `nfc_task`** | ⏳ **Next** | The `nfc` adapter component is still an empty stub. Reader is now wired. |
| **Speaker** | ⏳ **Next** | Synthesized beeps over I²S exist, but haven't been heard on the real speaker yet. |
| **Card programming** | ❌ Not started | Codec and `ntag_write` exist. Still needed: a provisioning mode in the engine, API and phone UI (see Phase 4). |
| WS2812 LEDs | 🟡 Firmware done | RMT + current limiter. Check whether the strip and level shifter are physically wired. On-screen LED preview bar for dev. |
| SD card | ❌ Not started | Storage is on LittleFS in internal flash for now (960 KB partition). Sounds will need SD or a small set of embedded clips. |
| Sound assets, Soundboard page | ❌ Not started | `/api/fx/sound` route exists |
| On-target Unity tests (`firmware/test/`) | ❌ Not started | The HIL smoke test over serial covers integration for now |
| Costume build, cards, soak, battery | ❌ Not started | |

---

## Done (Oct 13)

<details>
<summary>Phases 0–3 and most of Phase 5. Expand for the commit trail.</summary>

| Commit | What |
|---|---|
| `d8a8e08` | Phase 0: scaffold, host test harness, CrowPanel display bring-up |
| `20cf6ea` | Portable game core, Fare Paid + Rush Hour, host tests |
| `6a391b3` | Web API, config parsing, property tests, API contract fixtures |
| `d7b31f7` | `orca-sim`: scenario runner + golden transcripts, REPL, HTTP/WS server |
| `2a70a82` | `nfc_core` (PN532 protocol/driver, card payload, debouncer) + `fx_core` (LED effects, current limiter, WAV parser, mixer, beep synth) |
| `ab8129e` | Phone UI with Vitest contract tests and Playwright E2E |
| `e72f27d` | Phase 3: costume firmware on the CrowPanel with a simulated reader. HIL smoke test passing. |
| `10f10e3` | Team Lines, Last Stop, Fare Inspector, Lottery + generic game actions |
| `5b9956f` | Screen restyled after the real ORCA reader (black idle, artwork, "Good until 1:07 pm") |

</details>

---

## Phase A: Real reader and speaker (Oct 17–19), **in progress**

The hardware is now wired: PN532 on the I²C header (SDA 19 / SCL 20, DIP set to I²C) and the speaker on the onboard amp connector.

1. **PN532 I²C transport** in the `nfc` adapter. It implements `nfc::Transport` (`write`, `wait_ready` by polling the status byte, `read`) on the shared I²C bus at 100 kHz.
   - First light: `firmware_version()` logs `PN532 v1.6` on boot.
   - **Capture real frames** (firmware version, a passive-target response for an NTAG card and for a MIFARE card, a timeout) and add them to `host/tests` as fixtures next to the datasheet ones.
2. **`nfc_task`**: `sam_configure()`, then `poll_card()` every 50 ms → `TapDebouncer` → read pages 4–7 → `decode_payload()` → post a `TapEvent` to game_task. Health handling: after 3 failures, re-run SAM config, then report `ReaderFault` (the fault screen already exists).
   - Keep the simulated reader (console `tap`, `/api/sim/tap`) working alongside the real one.
3. **Speaker**: play the synthesized accept and reject beeps through the real speaker. Tune the volume and the beep tones by ear, and check for pops at start and stop.
4. **Latency**: log the time from a card coming into range to the beep starting. The target is under 300 ms.

**Checkpoint:** Tap a real card, hear the beep, see "Fare paid", and the tap appears in the phone's live feed.
**Test gate:** `make check` is green with the new real-frame fixtures. `make hil` still passes. 50 of 50 bench taps register, with no double taps from a card held in the field.

## Phase B: Card programming (Oct 19–21)

The goal is a stack of numbered cards where **card #n is player #n**, even on a freshly wiped costume.

1. **Engine**: a provisioning state. While it's armed, taps don't score. Instead the engine asks `nfc_task` to write `encode_payload({player})` to pages 4–5, **reads it back to verify**, registers the UID as that player, and moves on to the next number. Write failures (the card was pulled away too early) show "Hold card still" and retry on the next tap.
2. **API**: `POST /api/provision {start_no, count?}`, `POST /api/provision/stop`, and the provisioning state in `/api/status` and WS. Host tests plus contract fixtures. A scenario for "provision 3 cards, then play".
3. **Phone UI**: a "Program cards" page with the start number, the next number to write, a live log, and a Stop button. Vitest + Playwright (driven through `sim/tap`).
4. **Screen**: "Programming card #7 — tap next card" and a green check after each card is verified.
5. **Rules**: a card that already has a payload is not overwritten unless "overwrite" is ticked. Cards are not permanently locked (see the card notes in ARCHITECTURE §6.1).

**Checkpoint:** Program 10 cards in a row in under a minute. Wipe the state, tap card #4, and it comes back as player #4.
**Test gate:** Host and contract tests for provisioning. The HIL smoke test gains a provisioning step using the simulated reader.

## Phase C: Assets, storage and LEDs (Oct 20–23)

- [ ] **LEDs**: confirm the strip, the 74AHCT125 and the bulk cap are wired. Run every effect at the real brightness cap. Turn off the on-screen LED preview bar.
- [ ] **Sounds**: decide between an SD card and embedding a few short clips in flash (~300 KB at 22 kHz mono fits in the storage partition). Sounds needed: the accept and reject beeps (synth is fine), a Rush Hour sting, an Inspector sting, a Lottery drum roll, and a startup chime. `make check-assets` validates WAVs with the `fx_core` parser.
- [ ] **SD card** (optional): SPI mount, tap log CSV, an override for `sounds.json`. Fall back to LittleFS if there's no card.
- [ ] **Soundboard page** in the phone UI, using the existing `/api/fx/sound`.

**Checkpoint:** Each game event has its own sound and LED effect on the real hardware.

## Phase D: Costume build (Oct 20–26, in parallel)

- [ ] Enclosure shaped like the ORCA reader, with a display window, a "tap here" target over the PN532 antenna, a speaker grille, and a harness mount.
- [ ] **Antenna placement**: measure read range through the front material. Keep metal, the battery and the speaker magnet at least 2 cm from the antenna.
- [ ] LED ring around the tap target, diffused.
- [ ] Wiring: strain relief, a power switch, the battery in a pocket, and access to USB for reflashing.
- [ ] Card art: print stickers with ORCA-parody art and the **player number**, then program them in order (Phase B).

**Checkpoint:** You can wear it for 30 minutes and it still reads cards.
**Test gate (manual):** Read range ≥ 3 cm through the front panel, and 50 of 50 taps register while worn.

## Phase E: Remaining games (Oct 22–26, stretch)

Only once Phases A–C are done. Each game needs a `GameHarness` test file, a golden scenario and a pass on the property suite.

1. Route Collector
2. Golden Ticket

## Phase F: Hardening and dress rehearsal (Oct 26–29)

- [ ] **Soak test**: 2 hours of simulated taps at 2/s with 3 phones connected. Record the heap and stack high-water marks, and sample the webcam for display drift. No resets.
- [ ] **Long property run**: `make props-long` overnight.
- [ ] **Battery**: a full run-down on the real bank at the real brightness.
- [ ] **Failure drills**: unplug the PN532 mid-game, kill Wi-Fi, reboot mid-Rush-Hour, fill the storage partition. Each should degrade gracefully and stay in character. Turn each drill into a test where possible.
- [ ] **Dress rehearsal** with friends. Tune the defaults, and replay the tap log through `orca-sim` to try other settings.
- [ ] Freeze the firmware, tag a release, and keep a known-good `.bin`.

**Test gate:** The soak test passes, and `make check` plus `make hil` are green on the release tag.

## Phase G: Party kit (Oct 30–31)

- [ ] Two charged battery banks, a USB-C cable, **spare pre-programmed cards**, a pen, gaffer tape, and a laptop with the release build.
- [ ] A printed "how to play" card to hand out with the key cards.
- [ ] Bookmark `orca.local` / `192.168.4.1` on your phone.

---

## Schedule

| Dates | Focus |
|---|---|
| Sat 10-17 – Mon 10-19 | **Phase A**: real reader, speaker |
| Mon 10-19 – Wed 10-21 | **Phase B**: card programming. Order or print card art. |
| Tue 10-20 – Fri 10-23 | **Phase C**: LEDs, sounds, (SD) · **Phase D** begins |
| Sat 10-24 – Sun 10-25 | Costume build weekend. **Wearable MVP.** |
| Mon 10-26 – Thu 10-29 | Phase E stretch · **Phase F** hardening, dress rehearsal |
| Fri 10-30 – Sat 10-31 | Phase G: pack, charge, party |

**MVP line (needed to wear it on the night):** Phase A + Phase B + the enclosure from Phase D.
Everything else makes it better, but the costume works without it.

---

## Test tooling

| Command | Runs | When |
|---|---|---|
| `make check` | portability guard + host tests + Vitest + Playwright + firmware build | every change |
| `make scenarios` / `make scenarios-update` | golden scenarios / rewrite them after an intentional change | game changes |
| `make sim` / `make repl` | simulator with the live API / interactive REPL | UI work, game tuning |
| `make props-long` | 2000-seed property run | nightly, Phase F |
| `make hil` | flash + serial-driven smoke test + webcam color checks | before committing firmware changes |
| `make app-flash` | flash the app only, keeping saved state | day-to-day |
| (to add) `make soak` | 2 h on-device stress | Phase F |

## Open questions

1. **Party date(s).** Is there an event before Oct 31? That moves the MVP deadline.
2. **Number of players.** Roughly 20 or 100? This decides how many cards to program and print.
3. **SD card or no SD card?** The answer depends on how many sounds you want.
4. **LEDs.** Are the strip and the level shifter wired yet?
5. **Guest leaderboard.** Should guests see scores on their phones (STA mode, or an NDEF link on the card)?
