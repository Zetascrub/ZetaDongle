# LILYGO T-Dongle-S3

> **Origin:** this project was extracted from the Reconclave monorepo
> (`devices/t-dongle-s3/`). It began as a Reconclave fleet device but was split
> out as a standalone tool: a USB HID / BadUSB-style device is a poor fit for
> Reconclave's cooperating recon-and-evidence fleet model. It may still share
> the Reconclave *brand* (palette, mascot) but not the fleet protocol. See
> Reconclave's `docs/identity-and-scope-review.md` for the reasoning.

LILYGO's T-Dongle-S3 is a USB-A-plug-form-factor ESP32-S3 board. Role: USB HID /
BadUSB-style tool. Firing is gated to the device's own physical button — there is
deliberately no remote/autonomous keystroke path. If a cross-device trigger is
ever added, it must be gated behind a signed-scope/trust model with evidence
logging, never a bare "message arrives, keys get typed" path.

This device is used only against hardware the operator owns and has
authorization to test, on a local lab network.

## Hardware, confirmed against the real unit (not assumed from docs)

Public spec sheets for this board disagree on some details (some say 4MB
flash, others 8MB). Rather than trust any of them, every fact below was
verified directly against the physical unit:

- **Chip**: genuine ESP32-S3, QFN56, revision v0.2 — via `esptool chip-id`.
- **Flash**: 16MB — via `esptool flash-id`.
- **USB**: native USB-OTG (TinyUSB), not the separate "Hardware CDC and
  JTAG" peripheral — required for `USBHIDKeyboard`/`USB.h`. Needs
  `ARDUINO_USB_MODE=0` + `ARDUINO_USB_CDC_ON_BOOT=1` at build time (same
  flags `devices/cardputer-adv` already uses for the same reason).
- **RGB LED**: APA102 (separate data+clock lines), NOT WS2812 — data on
  GPIO40, clock on GPIO39. Confirmed from LILYGO's own
  `Xinyuan-LilyGO/T-Dongle-S3` `examples/led/led.ino`, not from third-party
  pinout pages that describe it as a plain single-wire "RGB LED".
- **LCD**: ST7735 80x160, MOSI=3 CLK=5 CS=4 DC=2 RST=1, backlight on
  GPIO38 (active-low: 0=on, 1=off).
- **SD card**: SD_MMC 4-bit mode, CLK=12 CMD=16 D0=14 D1=17 D2=21 D3=18.
- **Button**: GPIO0 — the universal ESP32 boot-strap pin. LILYGO's own
  `examples/usb_hid_keyboard.ino` references it via a `BOOT_PIN` macro, but
  that macro lives in a board-variant header that doesn't exist for
  PlatformIO's generic `esp32-s3-devkitc-1` board (which this project builds
  against, since no PlatformIO board entry exists for this hardware — same
  approach `devices/cardputer-adv` takes for its own unlisted M5Stack
  board). GPIO0 is hardcoded instead of depending on that macro.

## Toolchain

`platformio.ini` uses `platform = espressif32@6.7.0`,
`board = esp32-s3-devkitc-1` (generic stand-in board, flash size and
partition table overridden to match the real 16MB chip via
`board_upload.flash_size` / `board_build.partitions = default_16MB.csv`),
`framework = arduino`, and `lib_extra_dirs = ../../common` for shared
Reconclave code.

## Milestone 1: toolchain + basic I/O proof (done, verified on hardware)

`src/main.cpp` is deliberately minimal: no USB HID, no LCD, no network yet.
It only proves the build/upload pipeline works and that the APA102 LED and
the GPIO0 button respond the way LILYGO's own examples say they should.

Verification against the real unit:

- Compiled clean: RAM 9.5% (327680B budget), Flash 4.7% (6553600B budget).
- `pio run -t upload` wrote and hash-verified 308768 bytes at 0x10000.
  The upload's final auto-reset-via-RTS step threw
  `OSError: [Errno 71] Protocol error` — this is a known, cosmetic
  ESP32-S3-native-USB-CDC quirk (the port handle drops as the chip resets
  and the USB device re-enumerates) and does not indicate the flash write
  failed; the write+verify had already succeeded before that step ran.
- Confirmed the new firmware was genuinely running two ways that don't
  depend on winning the race against the app's one-shot boot banner over
  serial (`Serial` only prints once at startup, and CDC has no host-side
  buffering — a listener that opens even slightly late misses it):
  - **LED**: visually confirmed blinking blue (~2Hz), matching the
    firmware's idle blink loop.
  - **Button**: visually confirmed the LED turns solid red while the
    button (GPIO0) is held, matching the firmware's button-pressed branch.
- Serial console itself was not confirmed working live in this pass — every
  attempt to catch the boot banner or a button-triggered log line lost the
  race against the one-shot print. Not a known defect; if live serial debug
  logging is needed for a later milestone, add a repeating heartbeat line
  in `loop()` so a listener has something to catch regardless of timing.

## Milestones 2+3: USB HID keyboard + ST7735 display (done, verified on hardware)

Both landed together since they shared a debugging session. `src/main.cpp`
now also enumerates as a composite USB HID keyboard and shows a boot logo
on the ST7735.

### USB HID keyboard, physical-button-triggered only

Uses Arduino-ESP32's `USBHIDKeyboard` (from the `USB`/`USBHIDKeyboard`
libraries bundled with `framework-arduinoespressif32`, confirmed present
and `CONFIG_TINYUSB_HID_ENABLED=1` in this framework version's sdkconfig
before relying on it). The only trigger is the device's own physical
button — there's no network stack yet, so there is no remote/autonomous
path to a keystroke at all right now. That's deliberately conservative,
not the final design: once this device gains network connectivity, actual
remote-triggered HID injection must be gated by the same signed-scope/
trust model `devices/k230`'s `trust_policy.cpp` already implements
(verified scope + evidence logging), never a bare "message arrives, keys
get typed" path. The test payload is deliberately inert too — no Enter,
no modifier keys, plain text — so it can't execute a focused shell command
or trigger a shortcut, only visibly type a marker string.

Verified on hardware: `lsusb -v` shows a genuine composite device
(`bInterfaceClass 3` Human Interface Device + Communications/CDC Data),
and pressing the button three times in a row typed
`reconclave-t-dongle-s3 hid-test-ok #1/#2/#3` into a focused text field,
counter incrementing correctly each time.

One cosmetic gotcha hit along the way: `USB.productName()`/
`manufacturerName()` calls in `setup()` silently no-op under
`ARDUINO_USB_CDC_ON_BOOT=1`, because that flag makes the core call
`USB.begin()` automatically *before* `setup()` runs (so `Serial` works
immediately) — by the time our code could call those setters, `_started`
is already true and they're guarded no-ops. Fixed by setting
`USB_MANUFACTURER`/`USB_PRODUCT` as compile-time build flags instead,
since those become the `ESPUSB` constructor's default values before any
runtime code executes.

Also note: `USBHIDKeyboard` sends US-layout HID keycodes regardless of the
host's actual keyboard layout — confirmed on a UK-layout host, where the
`#` in the test string rendered as `£` (the UK-layout character at that
same physical key position). This is expected, not a bug — the only
"real" fix would be hardcoding a target layout assumption, which is the
wrong instinct for a general-purpose HID tool.

### ST7735 display

Uses Bodmer/TFT_eSPI, configured via build flags (not a `User_Setup.h`
file) copied verbatim from the library's own bundled
`User_Setups/Setup209_LilyGo_T_Dongle_S3.h` — this exact hardware already
has an official, tested config upstream, so there was no need to derive
pin/panel-variant settings from scratch. The 887x1774 source logo
(`/mnt/Storage/Coding/Misc/Mascot/logo-80-160.png`, aspect ratio already
matching the 80x160 panel exactly) was converted to a `PROGMEM` RGB565 C
array (`src/logo.h`) via:
```
convert logo-80-160.png -resize 80x160! -depth 8 RGB:logo.raw
```
followed by a small Python script packing each 3-byte RGB pixel into a
16-bit `0bRRRRRGGGGGGBBBBB` value (see git history for the exact script).

Two real bugs were found and fixed empirically, in order:

1. **Hang on `tft.init()`'s first SPI transaction.** Confirmed via a
   host-independent diagnostic: since the hang also somehow prevented USB
   CDC from coming up (no serial log reachable during the hang), the RGB
   LED was pressed into service as a poor-man's log instead — distinct
   colors set immediately before each risky call, so whichever color the
   LED froze on identified exactly which call hung (froze on the color set
   right before `tft.init()`). Root-caused by reading TFT_eSPI's own
   ESP32-S3 driver source (`Processors/TFT_eSPI_ESP32_S3.h/.c`, fetched
   locally by PlatformIO's `lib_deps`, not guessed from memory): the
   library's SPI-busy-check macro polls a *raw hardware register pointer*
   computed for a specific SPI host (`FSPI`/SPI2 by default), and something
   about that host's state on this board/core combination left the busy
   bit stuck. Fixed with the `-DUSE_HSPI_PORT` build flag, forcing the
   library onto the alternate host (`HSPI`/SPI3) instead — a documented,
   commonly-cited workaround for TFT_eSPI-on-ESP32-S3 hangs. This was
   tested empirically (add the flag, reflash, observe) rather than fully
   root-caused at the register level, since that would need live JTAG
   debugging this session didn't have set up.
2. **Wrong colors (cyan rendered as yellow).** `pushImage()` expects
   big-endian pixel words by default; the generated array is plain
   little-endian `uint16_t` (native ESP32 byte order). The fix is
   `tft.setSwapBytes(true)` before `pushImage()` — NOT touching the pixel
   data. (A wrong first attempt pre-swapped the R/B channels in the source
   data instead, which combined with the *real* underlying byte-order bug
   to produce a different wrong color, purple — a useful confirmation that
   the bug was byte-order, not channel-order, once the math was checked.)

### The upload workflow's real quirk: no auto-reset circuit

Every single reflash in this session needed the same manual two-step
dance, and this is a permanent fact about this hardware, not a one-off
glitch: the T-Dongle S3 is a bare USB-A-plug dongle with no auto-reset
transistor pair (the RTS/DTR-to-EN/BOOT circuit normal dev boards have).
`esptool`'s software auto-reset-into-bootloader trick is unreliable here
(fails with "No serial data received" more often than not once the app
has been running for a while — some subsequent USB CDC session against
the running app is enough to leave the state where it stops working). The
reliable procedure every time:

1. **To flash**: hold the button, unplug+replug the dongle while holding
   it, keep holding ~2 more seconds, release. This forces GPIO0 low during
   the chip's own power-on reset, entering the ROM bootloader
   deterministically (`esptool`'s own connect handshake needs this to
   succeed at all).
2. **After a successful upload**: `esptool`'s post-upload "hard reset via
   RTS pin" step frequently doesn't actually leave bootloader mode either
   (confirmed via `lsusb -d 303a:` showing PID `0x1001` "Espressif USB
   JTAG/serial debug unit" — the ROM's own identity, not the app's). A
   second, *plain* unplug/replug (no button this time) is needed to force
   a clean power-on boot into the newly-flashed app.

## Milestone 4: Zeta-themed web script editor + display dashboard (done, verified on hardware)

This is the device's first network stack. Consistent with the milestone
2+3 constraint quoted above, adding it does **not** open a remote/autonomous
path to a keystroke: the web UI can create, edit, and assign scripts to the
button, but has no "run" endpoint at all. Firing is still, exclusively, a
human pressing the physical button. See `src/main.cpp`'s header comment for
the full reasoning (it's the same one milestone 2+3 already stated, now
actually being tested by a real network stack existing).

Compiled clean (`pio run`: Flash 15.8%/1,034,733 bytes, RAM 17.7%/57,936
bytes of the 6.5MB app / 320KB RAM budgets) and confirmed on the real unit:

- **Upload**: this time `esptool`'s write+hash-verify completed with no
  `OSError` at all (milestone 1 hit one at the final auto-reset step, still
  harmless when it happens - see there) - and the documented no-auto-reset
  quirk reproduced exactly as described below: post-upload, `lsusb -d
  303a:` still showed PID `0x1001` (ROM bootloader identity), needing the
  plain unplug/replug to actually boot the new app.
- **USB identity**: `lsusb -v` after that replug shows manufacturer/product
  strings "Reconclave"/"Reconclave T-Dongle-S3" with `bInterfaceClass 3`
  (HID) + `2`/`10` (Communications/CDC Data) - the real composite app
  device, not the bootloader.
- **Serial heartbeat**, caught mid-stream same as milestone 1 (no race
  against the one-shot boot banner needed):
  `heartbeat: alive, fires=1, assigned="hello", ap=Zeta-Dongle-46D8` -
  confirms the AP came up, LittleFS mounted and persisted a saved+assigned
  script across the reflash/reboot, and a button press had already run it.
- **HID output**: confirmed by the operator - pressing the button with
  "hello" armed typed correctly into a focused field.
- **Display**: confirmed by the operator - boot animation played and the
  dashboard pages (brand/network/armed/stats) are readable and cycling
  correctly at 80x160.
- **Web UI**: confirmed by the operator - connecting to the
  `Zeta-Dongle-<XXXX>` AP, loading the page, and saving+assigning a script
  all worked smoothly, no rough edges reported.

### Standalone WiFi AP + script editor (`src/web_ui.*`, `src/web_ui_page.h`)

The device brings up its own WiFi AP (`WiFi.softAP`, SSID
`Zeta-Dongle-<last 4 hex of the AP MAC>`, password `TinkerHackFlash`,
hardcoded in `web_ui.cpp` — change and reflash for a deployment where a
fixed default passphrase isn't acceptable) rather than joining an existing
network: fully offline, no router/internet dependency, matching the
Malduino W web-interface model this was explicitly modeled on
(https://docs.maltronics.com/devices/malduino-w/web-interface) and this
project's existing "offline field tool" posture. The AP passphrase is the
entire access-control boundary for who can edit/arm scripts — there's no
separate login.

Browsing to the device's IP (shown on the display's network page, see
below) serves a single self-contained page (`src/web_ui_page.h`, generated
from a plain HTML/CSS/JS source plus an inlined base64 mascot PNG — see
that file's header comment for the regen steps) styled with the Zetascrub
palette sampled directly from `/mnt/Storage/Coding/Misc/Mascot/
Zeta_mascot_transparent.png`'s own "COLOUR PALETTE" swatch (cyan `#22e0f2`,
navy `#08283e`, tan `#b48353`, orange `#f6a102`). It lists saved scripts,
lets you edit/save/delete them, and assign one to the button — with an
explicit on-page note that there is deliberately no remote run button.

JSON API (`WebServer`/`ArduinoJson`, same libraries and request/response
idiom `devices/cardputer-adv/src/main.cpp` already uses — `server.arg
("plain")` + `deserializeJson`, `JsonDocument`/`.to<JsonArray/Object>()`,
not the older Static/DynamicJsonDocument API):

| Route | Method | Purpose |
|---|---|---|
| `/` | GET | The script editor page. |
| `/api/status` | GET | AP SSID/IP, assigned script, fire count, uptime. |
| `/api/scripts` | GET | List of saved script names + which is assigned. |
| `/api/script?name=` | GET | One script's body. |
| `/api/script` | POST | `{name, body}` — create/update (validates name, caps body at 8KB). |
| `/api/script/delete` | POST | `{name}` — delete (also clears the assignment if it was armed). |
| `/api/assign` | POST | `{name}` (`""` clears) — arm/disarm the button. |

A `runDuckyScript()`-blocking button press (see below) blocks
`WebServer::handleClient()` too, since `loop()` is single-threaded — the web
UI is intentionally unresponsive for the (capped) duration of a running
script, the same way a real BadUSB device's keystrokes have your full
attention while they're happening.

### DuckyScript-subset interpreter (`src/ducky_script.*`)

A parser/executor for the same script language family the USB Rubber Ducky
and (per its own web UI docs) the Malduino W use, so payloads from that
wider ecosystem's docs/examples port over directly. Supported: `REM`,
`STRING`/`STRINGLN`, `DELAY`, `DEFAULTDELAY`/`DEFAULT_DELAY`, `REPEAT`,
modifier combos (`GUI`/`CTRL`/`ALT`/`SHIFT` + a named key or single
character, e.g. `CTRL ALT DELETE`, `GUI r`), and named keys (`ENTER TAB ESC
SPACE BACKSPACE DELETE HOME END INSERT PAGEUP PAGEDOWN UP DOWN LEFT RIGHT
CAPSLOCK F1`-`F12`). Built directly on `USBHIDKeyboard`'s existing
`press()`/`release()`/`releaseAll()`/`print()` — no new keyboard-emulation
layer.

Bounded independent of anything a script claims, on top of (not instead of)
the button-only-firing constraint: max 500 lines, a single `DELAY`/
`DEFAULTDELAY` capped at 60s, `REPEAT` capped at 50, and a 120s total
runtime budget checked between every line/repeat iteration. These stop a
malformed or hostile *saved* script from wedging the device once a
legitimate button press starts it — they are not why remote firing doesn't
exist (that's the no-run-endpoint decision above).

### Script storage (`src/script_store.*`)

LittleFS, mounted (formatting on first boot) on `default_16MB.csv`'s spare
`spiffs`-labeled ~3.4MB data partition — no filesystem-image upload step
needed, `ScriptStore::begin()` calls `LittleFS.begin(true)` and creates
`/scripts/` itself. Each script is `/scripts/<name>.txt`; `/meta.json`
holds the assigned name and a fire counter that persists across reboots
(`last_fired_ms` is deliberately `millis()`-relative and does *not*
persist — it resets to "not fired yet" every boot, which the status
API/display both express as such rather than a stale absolute time).
Names are restricted to 1-32 chars of `[A-Za-z0-9_-]` (rejects path
traversal by construction, not by sanitizing).

### Display: boot animation + auto-cycling dashboard (`src/display_ui.*`, `src/mascot_assets.h`)

Answering "more than just a logo display": the GPIO0 button stays
dedicated to firing the armed script (per the button-only-firing decision),
so there's no spare input to drive a display mode switch — the dashboard
cycles on its own timer instead, no second button needed.

- **Boot**: the existing milestone 2+3 static logo shows first (unchanged),
  then a 6-frame animation cropped from `/mnt/Storage/Coding/Misc/Mascot/
  zetascrub_tail_pulse_boot.gif` (its glowing tail-band literally pulses
  down the tail across frames — the crop keeps the mascot + tail and drops
  the gif's own baked-in wordmark/background clutter, which read as an
  unreadable smear at 64px wide before that crop was applied).
- **Dashboard**, cycling every 3.5s: (1) brand page with a small circular
  Zeta headshot badge; (2) network page (AP SSID, passphrase, IP) so you
  don't need another device to find the URL; (3) armed-script page (name,
  or "none — use web UI to assign"); (4) stats page (fire count this boot,
  seconds since last fire).
- **Fired overlay**: a brief green "FIRED! count: N" screen right after a
  script runs (`flashFired()`), matching the existing LED-flash pattern —
  green LED+screen for a successful fire, orange LED for a button press
  with nothing assigned, red LED if the assigned script failed to load.

All art was flattened onto the same brand navy (`#08283e`) the dashboard
backgrounds use, so the circular badge and boot frames blit in with no
visible box around them — see `mascot_assets.h`'s header comment for the
exact ImageMagick regeneration commands (same plain-RGB565-packing
convention `logo.h` already established; watch the `-background` vs.
`-extent` ordering if regenerating — setting `-background` *after* `-extent`
silently pads with white instead, which is what happened on the first
attempt this session before the pipeline was fixed).

## Next steps (not started)

- Wiring actual keystroke-injection capability behind Reconclave's
  signed-scope trust model for any *cross-device* trigger path, should one
  ever be justified — advertised, logged, never autonomous. Milestone 4
  deliberately does not add this; it only makes button-only firing easier to
  configure.
- Nothing currently reads the fired-overlay/dashboard state back out over
  serial for scripted testing — worth adding if further verification turns
  into a repeated flash/test loop.
