# ORCA Reader Costume — Implementation Plan

Companion to [ARCHITECTURE.md](ARCHITECTURE.md). Today is **Tue 2026-10-13**, Halloween is
**Sat 2026-10-31**: 18 days. The plan aims for a **wearable MVP by Sun 10-25** and treats the
last week as polish and buffer. If your party is earlier (e.g. Sat 10-24), cut to the
"MVP line" below.

Each phase ends with a **demo checkpoint** (something you can watch working) and a **test
gate** (what has to pass before moving on). Testing details are in ARCHITECTURE §9.

**Two tracks run in parallel from day 1:**
- **Host track** (laptop only): portable core, tests, `orca-sim`, phone UI. It doesn't wait on hardware.
- **Device track**: bring-up, drivers, integration, costume.

They meet in Phase 3, where the same core code runs on the device.

---

## Phase 0: Foundations (Oct 13–14)

**Repo and test harness (host track)**
- [ ] `git init`, `.gitignore`, and the directory layout from ARCHITECTURE §4.
- [ ] `host/CMakeLists.txt` with GoogleTest/gmock via FetchContent, and an empty `hal/` with a `FakeClock`. One passing test.
- [ ] Guard: the host build fails if any ★ component includes an ESP-IDF header (compile without IDF include paths).
- [ ] `make check` runs the host tests. Add a GitHub Actions workflow if the repo is pushed.

**Hardware bring-up (device track)**
- [ ] Install ESP-IDF v5.3+. Create `firmware/` and set target `esp32s3`.
- [ ] `esptool.py flash_id`: confirm **flash size** (4 MB vs 16 MB) and PSRAM (octal, 8 MB). Write `partitions.csv` to match.
- [ ] Download the Elecrow CrowPanel 5.0" schematic. **Fill in the real pin table** in ARCHITECTURE §2.1.
- [ ] Solid-color fill on the RGB panel (red → green → blue). Verify it with a webcam frame.
- [ ] Order consumables now: **NTAG215 cards (×50+)**, 74AHCT125, 1000 µF cap, 330 Ω resistor, leads, a 2-port battery bank.

**Checkpoint:** The screen cycles through colors, as confirmed by the webcam. Flash and PSRAM sizes are known.
**Test gate:** `make check` is green in a clean clone. `idf.py build` succeeds.

## Phase 1: Portable core and host simulator (Oct 14–18, host track)

This is the most important phase. Write tests alongside the code (test-first for game rules).

1. **`hal` + fakes:** `Clock`, `Random`, `CardReader`, `AudioOut`, `LedOut`, `ScreenSink`, `FileSystem`, `KvStore`. Fakes: `FakeClock`, `SeededRandom`, recording outputs, `MemFileSystem`.
2. **`game_core`:** `PlayerRegistry`, `ScoreBoard`, the `Game` interface + `REGISTER_GAME`, `Engine`, `EventScheduler`, Feedback→Theme mapping, and snapshot save/restore with `.bak` fallback.
   - Tests: everything in the ARCHITECTURE §9.1 `game_core`/`EventScheduler` rows.
3. **`games/fare_paid`** using the `GameHarness` fixture.
   - Tests: first tap, tap during cooldown, the exact cooldown boundary, multiplier window, unknown card, banned player.
4. **Property suite** (§9.2), run against every registered game.
5. **`api`:** `handle(ApiRequest) → ApiResponse` with the `EngineGateway` interface. Routes: `status`, `games`, `game/*`, `players`, `sim/tap`, `events/trigger`, plus auth.
   - Tests: every route (happy path, validation, auth, 404/405). Also create the shared `api_fixtures/*.json`.
6. **`config`** parsing with defaults. Tests per §9.1.
7. **`orca-sim`:** scenario runner + golden transcripts, the CLI REPL, and `--serve` over cpp-httplib (HTTP + WS).
   - Starter scenarios: `fare_paid_basic`, `fare_paid_cooldown_edges`, `rush_hour_window`, `reboot_mid_game`, `unknown_card_open_enrollment`.

**Checkpoint:** You play Fare Paid in the `orca-sim` REPL, and `curl localhost:8080/api/status` works.
**Test gate:** Unit, property, and scenario tests are green. Coverage on `game_core`, `games`, and `api` is at least 85% lines (`gcovr`), to be a guide, not dogma.

## Phase 1D: Peripheral drivers (Oct 14–18, device track, parallel to Phase 1)

Build one component at a time. Any logic that can move out of a driver goes into a ★ component with host tests.

1. **`board` + `display`:** RGB panel with PSRAM framebuffers and bounce buffers, the LVGL 9 port, "Hello ORCA". Measure FPS and check for drift while Wi-Fi is up.
2. **`pn532_proto` (★) + `pn532` adapter:** Frame build/parse and checksums are host-tested first, using byte vectors from the PN532 datasheet. Then the I²C transport on hardware. **Capture real response bytes** and add them as host test fixtures. If I²C is flaky, tune the bus speed and timeouts first (there is no free UART on this board). Then add NTAG read/write.
3. **`storage`:** SD mount and a `FileSystem` adapter. The snapshot logic is already tested on the host against `MemFileSystem`.
4. **`fx_core` (★) + `audio`:** WAV header parser and mixer host-tested. I²S output with a RAM-cached beep, then streaming from SD.
5. **`fx_core` (★) + `leds`:** Effects and the current limiter host-tested. `led_strip` on RMT on GPIO 38 through the level shifter.
6. **Unity on-target tests** (`firmware/test/`) for each adapter: init, a round trip, and an error path.

**Checkpoint:** A "tap test" firmware. Tapping a card logs the UID, beeps, flashes the LEDs green, and shows the UID on screen.
**Test gate:** The on-target Unity app passes on the board. `pn532_proto` host tests include real captured frames.

## Phase 2: Phone UI against the simulator (Oct 17–21, host track)

The phone UI can be built before the firmware is integrated, because `orca-sim --serve` exposes the real API.

- [ ] `web/` with Preact + Vite + TypeScript. Vite dev server proxies `/api` and `/ws` to `orca-sim`.
- [ ] MVP page: status, start/stop/reset, a "simulate tap" button, and a live tap feed over WS.
- [ ] Vitest unit tests using the shared `api_fixtures` as mocks (contract tests, §9.4).
- [ ] Playwright E2E: start `orca-sim --serve --speed 10x`, then at a phone viewport: log in → select Fare Paid → start → inject taps → check the feed and leaderboard.
- [ ] Build step gzips the output and checks the size (< 150 KB) in `make check`.

**Checkpoint:** On your phone, on the same Wi-Fi as the laptop, you control a simulated game.
**Test gate:** Vitest and Playwright are green in `make check`.

## Phase 3: Integration, the MVP firmware (Oct 18–22)

The same ★ code that passed on the host now runs on the device.

- [ ] Task layout per ARCHITECTURE §5: queues, `game_task` owning the Engine, output tasks consuming commands.
- [ ] Device implementations of the `hal` interfaces (queue-backed outputs, `esp_timer` Clock, SD FileSystem).
- [ ] ORCA `Idle` and `Result` screen templates.
- [ ] `storage_task`: debounced snapshots, the tap log, and restore on boot.
- [ ] `net`: SoftAP + captive DNS + mDNS, the `esp_http_server` ↔ `api` adapter, and WebSocket. Serve the gzipped web UI.
- [ ] Serial console commands (`tap <uid>`, `status`), mirroring the sim API, for HIL testing without Wi-Fi.
- [ ] **HIL smoke test** `tools/hil/smoke.py` (§9.5): flash → wait for `READY` → sim taps → check the serial log → check the webcam frame color for green/amber/red.

**Checkpoint (MVP line):** Real cards play Fare Paid end to end. The phone controls the game.
Pulling power mid-game and re-plugging restores scores and cooldowns.
**If time runs out, this alone is a working costume.**
**Test gate:** The HIL smoke test passes 3 runs in a row. The `reboot_mid_game` scenario matches on both host and device (the same snapshot file restores identically).

## Phase 4: Costume build (Oct 19–25, in parallel with Phases 3 and 5)

- [ ] Enclosure: a foam-board or 3D-printed body shaped like an ORCA reader. Leave a display window, mark a "tap here" target over the PN532 antenna, and add a speaker grille. Make it mountable on a harness or chest plate.
- [ ] Antenna placement: test read range through the front material and keep metal and the battery away from it.
- [ ] LED placement (ring around the tap target plus accent strips). Diffuse them.
- [ ] Wiring: strain relief, a power switch, the battery in a pocket, and access to the USB port for reflashing and HIL tests.
- [ ] Print the cards with ORCA-parody art plus a **printed player number** (`tools/` generates the sheet).
- [ ] Provision the cards: provisioning mode, then tap each card in order so card #n = player #n.

**Checkpoint:** You can wear it, walk around for 30 minutes, and it still reads cards.
**Test gate (manual):** Read range ≥ 3 cm through the front panel, and 50 of 50 test taps register.

## Phase 5: More games and full phone UI (Oct 21–26)

Each new game follows the **definition of done** (§9.7): a `GameHarness` test file, at least one
golden scenario, and a pass on the property suite. Tune game feel in the `orca-sim` REPL before
touching hardware.

- [ ] Games, in priority order (stop wherever time runs out):
  1. Rush Hour (a preset)
  2. Team Lines
  3. Last Stop
  4. Fare Inspector
  5. Lottery / Last Call (good finale)
  6. Route Collector, Golden Ticket (stretch)
- [ ] Phone UI pages: Dashboard, Games (forms from ParamSchema, Vitest-tested against each game's schema), Players, Events, Soundboard, Settings, public `/board`. Each page gets a Playwright test.
- [ ] Admin PIN auth and clock sync from the phone (api tests cover expired and invalid sessions).
- [ ] Sound and asset pass: beeps, voice lines, Rush Hour and Inspector stings (22.05 kHz mono WAV, validated by the `fx_core` parser in a `make check-assets` step).

**Checkpoint:** You can switch between 3+ games from the phone without reflashing.
**Test gate:** `make check` is green, and the HIL smoke test passes with each new game selected.

## Phase 6: Hardening and dress rehearsal (Oct 26–29)

- [ ] **Soak test:** 2 hours of simulated taps at 2/s with 3 phones connected. Record the heap and stack high-water marks, sample the webcam for display drift. No resets.
- [ ] **Long property run:** 100k random tap streams per game overnight on the laptop.
- [ ] Full battery run-down test on the real bank with the real brightness settings.
- [ ] Failure drills: pull the SD card, unplug the PN532, kill Wi-Fi, reboot mid-Rush-Hour. Each should degrade gracefully and stay in character. Turn each drill into a host test or scenario where possible.
- [ ] Dress rehearsal with a few friends. Tune the defaults in `config.json`, and replay the rehearsal's tap log through `orca-sim` to try other settings.
- [ ] Freeze the firmware. Tag a release. Keep a known-good `.bin` and a spare SD card image.

**Test gate:** The soak test passes, and `make check` plus HIL are green on the release tag.

## Phase 7: Party day kit (Oct 30–31)

- [ ] Charge two battery banks. Pack a USB-C cable, spare cards, a pen for names, and gaffer tape.
- [ ] A printed "how to play" card to hand out with the key cards.
- [ ] Set the Wi-Fi mode for the venue, and bookmark `orca.local` / `192.168.4.1` on your phone.

---

## Test tooling summary

| Command | Runs | Where | When |
|---|---|---|---|
| `make check` | host unit + property (quick) + scenarios + Vitest + Playwright + `idf.py build` + size checks | laptop / CI | every change |
| `make sim` / `orca-sim --serve` | simulator with the live API | laptop | UI dev, game tuning |
| `make test-target` | Unity on-target adapter tests | board via USB | after driver changes |
| `make hil` | flash + smoke test + webcam checks | board + webcam | before merging firmware changes, before each release |
| `make soak` | 2 h on-device stress | board + webcam | Phase 6, release |
| `make props-long` | 100k-run property suite | laptop | nightly / Phase 6 |

## MVP line vs stretch

| Must (wearable on night) | Should | Stretch |
|---|---|---|
| Reliable taps, beep, green/red screen, LEDs | Rush Hour random bonuses | Route Collector, Golden Ticket |
| Fare Paid game with cooldown | Team Lines, Last Stop, Lottery finale | Guest-facing leaderboard over STA Wi-Fi |
| Phone: start/stop/reset, live feed | Full phone UI, Players page, Soundboard | Themes beyond ORCA (e.g. "Haunted Transit") |
| Snapshot/restore | Card provisioning on device | LVGL SDL screen simulator + screenshot tests |
| Host unit/scenario tests, HIL smoke test | Playwright suite, soak test | Coverage gating in CI |

---

## Open questions for you

1. **Party date(s).** Is there an event before Oct 31? That moves the MVP deadline.
2. **Number of players.** Roughly 20 or 100? This affects card count, snapshot size, and whether guests need the leaderboard (STA mode).
3. **Venue Wi-Fi.** Will your phone always be the controller on the costume's own AP, or should guests reach a leaderboard too?
4. **Card art.** Print parody ORCA art on sticker cards, or plain cards with numbers?
5. **Prizes.** Should there be a scripted end-of-night finale (Lottery / Last Call), or is it open play all night?
6. **CI.** Will this live on GitHub (Actions CI), or is a local `make check` enough?

## Immediate next steps

1. Scaffold the repo and the host test harness (Phase 0, host track).
2. Install ESP-IDF and check flash and PSRAM size on the connected board (Phase 0, device track).
3. Start `hal` + `game_core` + `fare_paid` test-first.
