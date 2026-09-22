# Hardware and firmware notes — measured, not estimated

Everything here was measured on the actual board on 2026-09-15 by a throwaway
spike (`spike/heap/`, removed once it had answered its question — the code is
in the git history, last present at 026bc09). Where the design doc guessed a
number, the measured one is recorded next to it.

## The board

| | |
|---|---|
| Chip | ESP32-D0WD-V3 rev 3, dual core @ 240 MHz |
| Flash | 4 MB |
| PSRAM | **none** |
| USB-serial | CH340 (`1A86:7523`) |
| Shipped firmware | Espressif ESP-AT 1.1.2 — overwritten on first flash |

### Uploading needs the BOOT button held

Auto-reset into the bootloader does not work on this board: esptool resets the
chip but GPIO0 is not pulled low, so it boots the app and esptool reports
`Wrong boot mode detected (0x13)`.

What works reliably (2026-09-19): put the chip into download mode *first*, then
flash without letting esptool reset it.

1. Hold **BOOT**, press and release **EN**, then release **BOOT**. The chip now
   sits in the bootloader; the screen does not change.
2. `pio run -e esp32 -t upload`. `platformio.ini` sets
   `board_upload.before_reset = no_reset` and `upload_speed = 115200`, so
   esptool neither tries the broken auto-reset nor drops the link.

`--after hard_reset` via RTS *does* work, so the new firmware starts on its own.

Two ways it fails: `pio run -t upload` gives up after ~11 s, before a human can
do the buttons; and at **460800** baud the CH340 link drops right after the
baud switch (`No more data to read from the serial port`). Releasing BOOT
before EN also misses download mode (`No serial data received`).

Permanent fix if it becomes tiresome: a 1 µF capacitor between EN and GND.

## Memory — the design's budget holds with ~2x headroom

Paper estimate was ~85 KB of ~300 KB: 40 KB TLS + 35 KB sprite + 8 KB parser.

| Measurement | Value |
|---|---|
| Free heap at boot | 351,448 |
| **Largest contiguous block at boot** | **114,676** |
| 35,840-byte lane sprite | allocated, every byte touched |
| Free with sprite held | 315,592 |
| Free after WiFi up | ~209,000 |
| **TLS handshake cost** | **~44,000** (est. 40,000) |
| Free with sprite + TLS + parse live | ~157,000 |
| Largest block at that moment | 94,196 |
| **Lowest heap across a full run** | **150,932** |
| Leak check | returns to boot value exactly after every request |

### The per-lane sprite was necessary, not merely tidy

Free heap is ~351 KB but the **largest single block is ~114 KB**, because the
ESP32 heap is split across regions. A full-screen 320x240x16bpp framebuffer
needs **153,600 bytes contiguous** and therefore *cannot* be allocated at all.

The design's 320x56 per-lane sprite (35,840 bytes) was the only approach that
was ever going to work. Do not "optimise" it into a full framebuffer later.

## The display is an ST7789, not an ILI9341

The module is the common red "2.8\" TFT 240xRGBx320 V1.1" SPI board with an SD
slot and a touch footprint (touch unpopulated). It is sold as ILI9341, and the
design doc assumed that. The unit on hand has an **ST7789** controller. Found
by a bring-up spike (`spike/display/`, also removed — see the git history),
2026-09-19.

Wiring is exactly as in design doc §2, and it is right: nothing had to change.

TFT_eSPI configuration that works (build flags, no `User_Setup.h` edit):

```
-DUSER_SETUP_LOADED=1
-DST7789_DRIVER=1
-DTFT_RGB_ORDER=TFT_BGR
-DTFT_INVERSION_OFF=1
-DTFT_WIDTH=240  -DTFT_HEIGHT=320
-DTFT_MISO=19 -DTFT_MOSI=23 -DTFT_SCLK=18 -DTFT_CS=15 -DTFT_DC=2 -DTFT_RST=4
-DSPI_FREQUENCY=27000000
```

`setRotation(1)` and `(3)` give a clean 320x240 landscape; all four rotations
fill the glass edge to edge. The backlight on GPIO32 is driven HIGH by the app.

How each wrong setting looks, so it can be recognised again:

| Setting | Symptom |
|---|---|
| `ILI9341_DRIVER` | Landscape drawn un-rotated into 240 columns; the other 80 are static. Other rotations cut up / off-centre. Red and blue swapped. |
| ST7789, default inversion (ON) | Whole image a photo negative: black background shows white. |
| ST7789, default colour order (RGB) | Red and blue swapped. |
| Both defaults together | Red→yellow, green→magenta, blue→cyan, yellow→red. Looks random; it's inversion plus swap. |

The panel **does not answer reads**: RDDID (`0xD3`) returns `00 00 00` and
RDDST returns constant garbage. So the controller can't be detected in software;
identify it by rendering, as above. Don't build anything that depends on
reading back from the panel.

## Display performance

Measured on 2026-09-19 over a 225 s capture covering every scene in both themes,
SPI at 27 MHz (the default set in `platformio.ini`):

| Measurement | Value |
|---|---|
| SPI clock | 27 MHz |
| **Worst draw time per frame** | **59 ms** |
| Frame rate (all scenes) | 14.9–15.1 fps |
| **Lowest heap (min-ever)** | **311,588 bytes** |
| Largest contiguous block | 110,580 bytes |
| Flash used | 25.3% (331,741 of 1,310,720 bytes) |
| RAM | 6.9% |

The frame budget is 66 ms per frame at 15 fps; 59 ms leaves 7 ms of headroom.

The band renderer redraws the board in 48-row horizontal bands, each 320×48×2
bytes. That is **30,720 bytes per band** against the design spec's estimate of
35,840 (320×56), with **5 pushes per frame** to refresh the screen. This keeps
memory flat — no full-screen framebuffer.

SPI 40 MHz was not tried: 15 fps is met at 27 MHz and 40 MHz remains as headroom
for when WiFi and TLS run alongside.

### Double-precision sin() is software on this chip

The first measurement on 2026-09-19 showed the Ghibli theme at 96 ms per frame
(10.3 fps) in the 1–2 lane scenes, while transit ran at 54–57 ms. The cause:
`hill()` and `shore()` call `sin()` on doubles, and the ESP32's FPU is
single-precision only, so those calls are emulated in software. The band
renderer redraws each lane once per band, which meant about 3,000 software
`sin()` calls per frame.

Precomputing them into lookup tables (`hill_at` and `shore_at` in
`lib/core/src/shapes.cpp`, filled from the same functions so the values cannot
diverge) brought Ghibli to 58–59 ms per frame and unlocked 15 fps across all
scenes. The lesson: keep `double` maths out of the per-frame path.

## Flash is the tighter constraint

| Build | Flash used |
|---|---|
| Bare Arduino + heap probe | 265 KB (20%) |
| \+ WiFi, TLS, HTTPClient, ArduinoJson | **917 KB (70%)** |

70% of the default 1.31 MB app partition is gone before TFT_eSPI, the web
portal, or the sprite data exist. **The firmware will need a custom partition
table** — `huge_app.csv` or a hand-rolled one, noting OTA is a non-goal so a
single app partition is fine.

## The bug that would have shipped: chunked transfer-encoding

AT's GTFS API responds with `Transfer-Encoding: chunked`. The bytes on the wire
begin:

```
2561\r\n{"data":[{"type":"stoptrip", ...
```

The standard ESP32 idiom — `deserializeJson(doc, http.getStream())` — reads
`2561`, parses it as a valid JSON **number**, stops, and returns
`DeserializationError::Ok`. The document is a number, `doc["data"]` is null, and
the board shows **zero departures with no error, forever**.

The silent failure happens specifically **when parsing with an ArduinoJson
filter**, which the firmware always does (to keep the document small). With
ArduinoJson 7.4.3, measured across all three combinations:

| Parse | Body | Result |
|---|---|---|
| filtered | chunked | `Ok`, 0 rows — **silent** |
| filtered | clean (de-chunked) | `Ok`, 1 row |
| unfiltered | chunked | `InvalidInput` — loud |

Filtering is what turns a loud failure into a silent one: without a filter the
chunk-size token derails the parse enough to be rejected outright; with a
filter it's accepted as a lone number and the empty result looks like success.

Measured on hardware, same URL, same filter, back to back:

```
A. without de-chunking:  deserializeJson SUCCESS,  0 departures   <-- silent
B. with de-chunking:     deserializeJson SUCCESS, 19 departures
```

19 matches what `curl` returns for that URL. The fix is a small Stream wrapper
that unwraps chunk framing while keeping memory flat — it now lives in
`lib/core/src/dechunk.{h,cpp}`. `test/test_dechunk` pins both the bug (filtered
+ chunked without the wrapper) and the cure (filtered + chunked through it), so
neither regresses silently again.

### But the two AT APIs differ — branch, never assume

| API | Framing | `http.getSize()` |
|---|---|---|
| `gtfs/v3/...` | `Transfer-Encoding: chunked` | `-1` |
| `realtime/legacy/tripupdates` | `Content-Length` | e.g. `2176` |

De-chunking a **non**-chunked body is equally broken: the wrapper reads
`{"status"...` as a hex chunk header, gets 0, and declares the stream finished,
producing `EmptyInput`. Observed exactly that when the wrapper was applied
unconditionally.

So the client must branch on `http.getSize() < 0` at runtime rather than on
which endpoint it thinks it is calling.

## The `tripid` filter really is load-bearing

`GET /realtime/legacy/tripupdates` with **no** `?tripid=` returned
**766,473 bytes** — the whole Auckland network. Streaming it with an ArduinoJson
filter still drove free heap from ~160,000 down to **51,800**, largest block to
42,996, and ended in `IncompleteInput`.

With `?tripid=` and four trips the same endpoint returns **2,176 bytes**.

The design already said the filter was "load-bearing, not an optimisation".
That is now measured: unfiltered, the board comes within ~50 KB of the floor and
fails messily rather than cleanly.

## NTP and DST work

`configTzTime("NZST-12NZDT,M9.5.0,M4.1.0/3", "pool.ntp.org")` gave
`2026-09-15 20:42 NZST isdst=0` — correct. NZDT begins 27 September 2026; the
rule is in place and should flip on its own. Worth re-checking on the 28th.

## End-to-end, verified on hardware

The whole data path ran on the board on 2026-09-15 at 22:05, in one pass, with
the sprite held throughout:

```
A. stoptrips, no de-chunking              ->  0 departures   (silent failure)
B. stoptrips, de-chunked                  -> 11 departures
     22:04 O-W-201 dir 0 / 22:06 O-W-201 dir 1 / 22:26 E-W-201 dir 0
C. realtime ?tripid=, content-length      ->  4 entities
     delays -6, +16, -52, +66 seconds
```

The runtime branch picked `chunked -> de-chunking` for GTFS and
`content-length -> direct` for realtime within the same run, which is the
behaviour the firmware needs. Lowest heap across the whole pass: **151,032**.

Those delays are live: a train 52 s early and another 66 s late. Schedule alone
would have been wrong by over a minute in both directions.

## Live data on the board

Measured on the board on 2026-09-19, SPI at 27 MHz, WiFi and TLS live against
the real AT API:

| Measurement | Value |
|---|---|
| Frame rate | 15.1–15.2 fps |
| Worst draw time per frame | 55–57 ms (against a 66 ms budget) |
| Lowest heap (min-ever) | 94,944 bytes |
| Largest contiguous block | 94,196 bytes |
| Steady-state free heap | ~157 KB |
| Fetch task stack (core 0, 16,384 B) | 12,200 B free high-water mark — 4,184 B used at peak (16,384 − 12,200); the task performs the TLS handshakes, so this peak includes them |
| Live build flash | 75.6% (~990 KB of 1.31 MB) |
| Live build RAM | 25.8% |
| Demo build flash | 26.7% |

SPI stayed at 27 MHz: 15 fps held with the network running, so the 40 MHz
headroom noted above was not needed.

The lowest heap seen anywhere in this plan was **149,444 bytes**, during a TLS
handshake in `setup()` (the earlier self-check) — lower than the 157 KB
steady-state figure above but well clear of the ~95 KB floor seen during a live
fetch.

### TLS: the pinned root, and why the root

The board pins **DigiCert Global Root G2**, SHA256
`CB:3C:CB:B7:60:31:E5:E0:13:8F:8D:D3:9A:23:F9:DE:47:FF:C3:5E:43:C1:14:4C:EA:27:D4:6A:5A:B1:CB:5F`,
valid to 2038. The root is pinned rather than the leaf because the leaf
certificate expires in March 2027 — pinning it would mean the board silently
stops trusting AT's API a year from now. The root is stable for over a decade.

### NZ time rules live outside the standard library

Windows' UCRT (used for `pio test -e native`) mis-handles the POSIX TZ string:
it placed 2026-09-28 12:00 NZDT at 11:00 UTC, an hour wrong. So the NZ
DST rules are hand-implemented in `lib/core/src/nztime.cpp` rather than left to
the C library's TZ parsing.

### Flash headroom

Flash is at 75.6% for the live build. The portal (spec §3, not yet built) will
likely need `huge_app.csv` rather than the default partition table.

### First boot needs a wait for WiFi association

The first fetch attempt on first boot returned HTTP `-1` for both lookups: it
ran before WiFi association had completed. It succeeded once the code was
changed to wait for association rather than just for `WiFi.begin()` to
return.

## ESP32-S3 SuperMini — a second target, confirmed on hardware 2026-09-21

Everything above was measured on the classic ESP32. `platformio.ini` also
carries an `esp32s3` / `esp32s3_demo` pair for an **ESP32-S3 SuperMini**
(ESP32-S3FH4R2: 4 MB in-package flash, 2 MB in-package quad PSRAM, native
USB, and — unlike the classic board — a populated resistive touch
controller). Both live and demo builds run end to end: WiFi, AT data, the
display, and touch.

Why the S3 and not a C3: the classic build already sits at ~7 ms of frame-time
headroom at 15 fps with the network task on the second core. The C3 is single
core with no FPU. The S3 keeps both, and adds PSRAM as headroom.

### Wiring

Every pin is on the SuperMini's edge header. The map avoids the strapping pins
(0, 3, 45, 46), the native-USB pins (19, 20), and GPIO26–32, which are wired to
the in-package flash and PSRAM.

| Signal | GPIO | Notes |
|---|---|---|
| SCK | 12 | Shared with touch |
| MOSI (SDI) | 11 | Shared with touch |
| MISO (SDO) | 13 | Shared with touch |
| Display CS | 10 | |
| DC / RS | 9 | |
| RESET | 8 | |
| Backlight LED | 7 | LEDC PWM, as on the classic board |
| Touch CS (T_CS) | 6 | |
| Touch IRQ (T_IRQ) | 5 | Wired but unused; polling works fine at 15 fps |
| VCC / GND | 3V3 / GND | |

The display pins, and the backlight pin too, are build flags rather than
constants in `src/` — `src/backlight.cpp` takes `BACKLIGHT_PIN` and falls back
to 32, the classic wiring, when nothing defines it. There are no other
hard-coded GPIOs in the firmware.

### Why the env looks the way it does

- **`board = esp32-s3-devkitc-1`, with the flash size corrected.** There is no
  stock definition for the N4R2 part. The stock board is an 8 MB, no-PSRAM N8,
  so `board_upload.flash_size`, `board_upload.maximum_size` and the partition
  table all have to be overridden; its `default_8MB.csv` would not even fit.
- **`board_build.arduino.memory_type = qio_qspi`.** PlatformIO builds this
  string as `<flash mode>_<psram type>` and it selects which prebuilt Arduino
  SDK gets linked. The FH4R2 is quad flash + quad PSRAM. Getting it wrong
  means a board that boots without PSRAM, or crashes.
- **`-DARDUINO_USB_CDC_ON_BOOT=1`.** The S3 talks to the host over native USB.
  Without this the serial port stays silent and the whole boot log — including
  the IP the portal prints — is lost. `ARDUINO_USB_MODE=1` already comes from
  the board definition.
- **`board_build.partitions = huge_app.csv`**, for the reason in "Flash is the
  tighter constraint" above.
- **No `before_reset = no_reset`, no 115200 upload speed.** Those exist for the
  classic board's CH340 and its broken auto-reset. Neither applies here. If
  auto-reset ever does fail on a SuperMini, hold BOOT while tapping RESET.
- **`-DTOUCH_CS=6` and `-DSPI_TOUCH_FREQUENCY=2500000`** enable TFT_eSPI's
  built-in XPT2046 support over the display's own SPI bus.

`platform = espressif32@7.1.3` pins **Arduino core 2.0.17**, which is why
`backlight.cpp` can keep using `ledcSetup()` / `ledcAttachPin()`: core 3.x
removed them. Anything that bumps the platform has to revisit that file.

### Chip and PSRAM

`esptool.py flash_id` on the actual board reports `ESP32-S3 (QFN56) rev v0.2`,
`Embedded Flash 4MB (XMC)`, `Embedded PSRAM 2MB (AP_3v3)` — exactly the
SuperMini's advertised ESP32-S3FH4R2, and exactly what `board_upload.flash_size`
and `board_build.arduino.memory_type = qio_qspi` assume. (The vendor listing's
C3/RISC-V description, mentioned as a risk when this env was first added, was
simply wrong.)

### Two bugs found bringing the display up

1. **Boot panic** — Guru Meditation, `StoreProhibited`, backtrace in
   `TFT_eSPI::begin_tft_write() → writecommand() → init() → setup()`. Without
   `USE_HSPI_PORT` (or `USE_FSPI_PORT`), TFT_eSPI's ESP32-S3 driver
   (`Processors/TFT_eSPI_ESP32_S3.c`) defaults to a *reference* to the Arduino
   core's global `SPI` object (`SPIClass& spi = SPI;`) instead of constructing
   its own. Whether that reference is safe to use depends on C++
   static-initialization order across separately-compiled translation units,
   and on Arduino core 2.0.17 (what `espressif32@7.1.3` ships) it is not: the
   board crashed the instant the first SPI transaction ran, even though the
   build itself succeeded. Documented for this exact library-and-core
   combination in [Bodmer/TFT_eSPI#3329](https://github.com/Bodmer/TFT_eSPI/issues/3329)
   and [espressif/arduino-esp32#9618](https://github.com/espressif/arduino-esp32/issues/9618).
   Fix: `-DUSE_HSPI_PORT`, now in the `esp32s3` build flags — it makes
   TFT_eSPI construct its own `SPIClass` instead of touching the shared
   global. (Checked separately: on this driver, the Arduino-ESP32 SPI layer
   always routes through the GPIO matrix regardless of which host you pick,
   so choosing `HSPI` over `FSPI` costs nothing electrically — it's purely
   the fix for the static-init crash.)
2. **Wrong driver** — with the panic fixed, the panel came up solid white: no
   crash, demo scenes cycling normally in the serial log, just no image. This
   unit's panel is a genuine **ILI9341**, not the ST7789 the classic reference
   build has: swapping `-DST7789_DRIVER=1` for `-DILI9341_DRIVER=1` (now in
   the `esp32s3` build flags) fixed it outright — correct colours and
   geometry, no further `TFT_RGB_ORDER` or `TFT_INVERSION_OFF` changes
   needed. Confirms the note above ("The display is an ST7789, not an
   ILI9341"): the controller can't be detected in software, and different
   sourcing runs are different chips. A blank white screen with no crash is
   the recognised symptom of "wrong driver entirely", distinct from the
   inverted/swapped-colour symptoms in that section's table.

The band renderer stays in internal RAM on the S3 too. PSRAM is slower, and
the bands already hit 15 fps — do not move them without re-measuring.

### Touch

`src/touch.{h,cpp}` calibrates on first boot (`tft.calibrateTouch()` — touch
each corner as prompted) and stores the result in NVS under its own
`touch`/`cal` namespace, deliberately separate from `config.cpp`'s
`board`/`cfg`: this is device-local and never round-trips through the web
setup page.

Two things worth knowing before touching this code:

- **Raw reads are noisy at the moment of contact.** A single physical tap can
  read down/up/down across a couple of frames rather than staying cleanly
  down — each read a genuine edge, but taken together, enough to fire (and
  silently cancel out) more than one UI action per tap. Anything that acts on
  a touch should go through `touch_debounce()`, not a raw `touch_read()`
  edge: it requires 2 consecutive down reads and a 300ms cooldown before
  reporting a confirmed press. The bring-up serial log deliberately keeps
  printing every raw edge instead — seeing that flicker is the point of it.
- **Calibration accuracy, not a hard dead zone.** An inaccurate first
  calibration made touch appear to fail within ~20px of every screen edge
  (`getTouch()` rejects anything that maps outside `0..width` / `0..height`
  rather than clamping it, so a tap beyond the calibrated range is silently
  dropped). `calibrateTouch()` draws its corner targets right at the literal
  screen edges, which a fingertip can't hit precisely, so an imprecise
  calibration carries that error into every touch afterward. Recalibrating
  fixed it — touch now reaches close to the true edges. Type `c` + Enter in
  the serial monitor (`touch_poll_recalibrate()`) to redo calibration without
  a reboot or losing stops/theme/lane order; touch the corner targets as
  centred and deliberately as possible, a stylus may do better than a
  fingertip.

### Touch UI: reorder lanes

A status-bar chevron (`src/reorder_ui.{h,cpp}`) toggles reorder mode, which
reveals up/down chevrons at each lane's bottom-right corner; tapping one swaps
that lane with its neighbour. This only permutes a new `config_lane_order()`
(`src/config.cpp`) — it never touches `config_watches()` or the fetch task's
data, which is what makes it safe to change live rather than needing the
usual "save and reboot" the web setup page uses. Applied live and persisted
to NVS on every swap, the same pattern `config.cpp` already used for the
theme.

Chevron and toggle positions sit at their natural spots — toggle inside the
status bar, chevrons in each lane's actual bottom-right corner — now that
recalibrating fixed the touch accuracy problem above; an earlier pass had
them inset further out, to clear edges an inaccurate calibration couldn't
reach.

### The v2 config migration, confirmed 2026-09-22

The board carried a v1 config in NVS when the grouped schema landed. On the
first boot afterwards it logged `config: migrated to the grouped schema`,
and on every boot since it logs `config: ... (nvs)` with no migration line -
which is what says the blob write landed rather than the string being
re-read and re-migrated each time. A watch added through the setup page
afterwards survived a reboot, so the adapted page round-trips v2 as well.

Worth knowing if this ever has to be debugged again: because
`watch_config.h` holds the same stops as the saved config, a failed
migration would have fallen back to compiled defaults and looked *identical*
on the panel. `(nvs)` versus `(compiled defaults)` in the boot log is the
only thing that tells them apart.

### Live performance, measured 2026-09-22

Three lanes, live data, WiFi and TLS up, on the S3:

| Measurement | S3, live | Classic ESP32, for comparison |
|---|---|---|
| Draw time per frame | **68-72 ms** | 59 ms worst |
| Frame rate | **14.0-14.6 fps** | 14.9-15.1 fps |
| Lowest heap (min-ever) | **144,428** | 150,932 |
| Largest contiguous block | 2,031,604 (PSRAM) | 94,196 |

The S3 draws a frame *slower* than the classic board does, and enough to
miss the 66 ms budget 15 fps needs - hence 14.x rather than 15.0. It is not
visible on the glass and nothing was done about it, but it is worth knowing
before anyone assumes the faster chip is faster at this: the renderer is
bound by pushing 5 bands over SPI, not by the CPU, and the S3 gains nothing
there.

Heap holds up: the lowest reading is in the same place as the classic
board's, and PSRAM leaves a 2 MB contiguous block spare that nothing yet
uses.

### Touch UI: the settings page

A cog left of the reorder toggle (`src/settings_ui.{h,cpp}`) opens a
full-screen page over the board with three things on it:

- **Theme** — taps cycle it, live, as the web page's theme select already did.
- **Brightness** — `-`/`+` in ~10% steps. This is the first thing that ever
  actually drives `backlight_set()` with anything but 255, and it **confirmed
  the LEDC dimming path on hardware** (2026-09-22) — the design assumed it
  from the start, and nothing had exercised it until now. Clamped at
  `BRIGHTNESS_MIN`: a board dimmed to nothing looks broken, and the only way
  back is a setting you can no longer read.
- **Group** — the configured groups as chips across one row; tapping one
  makes it active. Chips rather than a row each because four groups and four
  lanes both need the height, and at a size worth tapping they do not both
  fit as lists. This is the one control here that cannot apply live: a
  different group means different stops, and the fetch task is reading the
  current ones on core 0 with no lock, so it persists and reboots - the same
  save-and-restart the setup page has always used for watches. Tapping the
  active chip is refused rather than costing a pointless reboot.
- **Lanes** — an on/off row per published watch in the active group.

The lane toggle is a *display*-level hide, which is what lets it be instant.
The fetch task keeps fetching a hidden watch, so switching one back on shows
its departures immediately instead of waiting out a fresh resolve; only the
next boot actually drops it, when the persisted `enabled` bit feeds
`cfg_publish()` as it always has. The web page's "On" checkbox is the same
bit seen from the other end — it's what gets *fetched*, and still wants the
usual save-and-reboot. `config_set_lane_visible()` refuses to hide the last
visible lane, and serialises from a copy of the config rather than mutating
`g_cfg`, so neither the portal on core 0 nor the fetch task sees anything
change under them.

Hiding a lane changes how many lanes are on screen, so it also changes the
lane geometry and the size class — and `config_lane_order()` is a permutation
over *every* published watch, hidden ones included. `Ui::draw()` therefore
builds a slot list each frame (which watch each visible lane shows, and where
in `order()` it came from) and hands `reorder_ui_touch()` that mapping, so a
swap moves entries at the right indices rather than at screen slots. Without
that, hiding a lane would quietly reshuffle the ones still showing.

Brightness lives in its own NVS key (`board`/`bright`) rather than the Config
schema — no schema version bump, nothing for the web page to round-trip. It
can move into the schema whenever brightness wants to be settable from the
browser too.

Confirmed on hardware 2026-09-22: the page opens, brightness visibly dims and
brightens, and the rows are comfortable to hit — at 28-30px tall they are a
much easier target than the reorder chevrons, which needed two rounds of
position tuning.

Two things about DEMO_MODE, which never calls `config_begin()`: the settings
page's Lanes section is empty there (no watches configured), and cycling the
theme changes the page's own reading of it but not the board behind it, since
demo scenes carry their own theme. Both work normally on the live build.

## Still to verify on hardware

- The WiFi-outage path: pull WiFi, expect `stale Nm` with the last good data
  kept, the lanes dimmed and the vehicles still animating, then recovery without a reboot when
  WiFi returns. This has **not** been performed on hardware. The code paths
  were reviewed but not observed running.
