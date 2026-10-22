<div align="center">

<img src="assets/zeta.png" alt="Zeta, the Reconclave mascot" width="140">

# ZetaDongle

### Pocket payloads. Physical intent. Fleet-aware.

[![Hardware](https://img.shields.io/badge/hardware-LILYGO_T--Dongle--S3-0e222e?style=flat-square)](#capability-matrix)
[![Build](https://img.shields.io/badge/build-PlatformIO-00cdd7?style=flat-square)](#build)
[![Status](https://img.shields.io/badge/status-development_preview-ffaa1c?style=flat-square)](#capability-matrix)
[![Family](https://img.shields.io/badge/family-Reconclave-fff2d7?style=flat-square)](https://github.com/Zetascrub/Reconclave)

**A pocket-sized USB automation tool and Reconclave node for LILYGO
T-Dongle-S3.**

[Reconclave](https://github.com/Zetascrub/Reconclave) ·
[FieldDeck](https://github.com/Zetascrub/FieldDeck) ·
[Relay](https://github.com/Zetascrub/Relay) ·
[Sightline](https://github.com/Zetascrub/Sightline) ·
[Command](https://github.com/Zetascrub/Reconclave-Command)

</div>

ZetaDongle works on its own as a physically triggered payload device with local
storage, loot management, SD-backed USB mass storage, and a browser-based
payload builder. In a trusted Reconclave fleet it can advertise status and
capabilities while keeping local button actions separate from remote work.

> Use ZetaDongle only on hosts you own or are explicitly authorised to test.
> Local payloads can run from the button or an explicitly enabled USB attach
> trigger. Hosted direct HID requires enrollment in the intended instance.

## Relationship to Reconclave

- **Standalone:** payload creation, arming, physical execution, audit records,
  loot storage, and USB mass storage do not require a coordinator.
- **Collective:** Wi-Fi node mode uses the Reconclave protocol and common node
  identity so the dongle can be discovered and managed as part of a fleet.
- **Shared contract:** `shared/protocol/` and `shared/identity/` are vendored
  snapshots of [Reconclave](https://github.com/Zetascrub/Reconclave), which
  remains authoritative for protocol and visual identity definitions.
- **Safety boundary:** local USB auto-run is off by default. The hosted direct
  queue requires the device's instance credential; LAN fleet HID still requires
  HMAC authentication and signed scope verification.

## Firmware overview

Dual-role firmware for the LILYGO T-Dongle-S3. One image operates as both a
trust-governed Reconclave fleet node and a self-contained field device inspired
by the payload workflow of devices such as the Bash Bunny. Use it only with
systems the operator owns or is authorised to assess.

## Operator experience

The Dongle always exposes a local management AP and responsive control deck. It
may simultaneously join a provisioned Reconclave network and advertise itself
as a fleet node.

- **Standalone:** create, edit, delete, and arm named DuckyScript payloads.
  Press the button or explicitly enable a once-per-boot USB attach trigger.
- **Node:** advertise and receive Reconclave capabilities. An enrolled dongle
  polls its hosted instance for direct HID commands. The older LAN fleet HID
  path still requires a provisioned link and signed scope.
- **Storage:** persistent LittleFS payload library, loot folder, run/failure
  counters, retained armed selection, optional microSD, and SD backup.
- **Local UI:** 80×160 Zeta-theme display with boot identity, mode/link status,
  armed-payload state, storage, and activity pages.
- **Web UI:** responsive Payloads, Loot, and Device views. There is deliberately
  no network Run button.

Connect to `Reconclave-XXXX` with password `reconclave-setup`, then browse to
`http://192.168.4.1/`.

## Payload workflow

1. Open **Payloads**, create or select a named payload, and save its DuckyScript.
2. Choose **Arm for button**. The display changes from `SAFE` to `ARMED`.
3. Focus the intended field on the authorised host and release the button once.
4. The display and LED report completion or failure; counters persist.
5. Choose **Disarm** when finished.

A long press (1.8 seconds) disarms the payload and turns off USB auto-run
without opening the control deck.

To run the armed payload on USB attach, send `autorun on` over the dongle's USB
serial console during preparation. It runs once per boot after USB HID is ready.
Send `autorun off` to return to button-only local execution. This setting is
stored on the dongle; no command is needed on the target computer. While it is
on, the management page cannot edit or change the armed payload; disarming
remains available. For remote
execution, enroll the dongle in a hosted instance and queue DuckyScript from
its dashboard. The dongle polls that queue over HTTPS and types on its USB host.

The interpreter is bounded to 500 lines, 60 seconds per delay, 50 repeats, and
120 seconds total runtime. USB keyboard output uses US-layout HID positions.

The control deck includes a guided builder for safe text, Linux file-copy,
Windows file-copy, and Linux terminal-command templates. Generated scripts land
in the normal editor for review and validation; the builder never saves or arms
them automatically.

Payloads may declare up to 24 readable variables and substitute them into text:

```text
DEFINE SOURCE /home/operator/Documents/example.txt
DEFINE DESTINATION /run/media/operator/RECONCLAVE/example.txt
STRINGLN cp -- "{{SOURCE}}" "{{DESTINATION}}"
```

Variable names are limited to 24 letters, digits, or underscores. Undefined or
unterminated placeholders are rejected with their source line before saving.

On an empty first boot, three unarmed examples are installed:

- `01-hello-safe`: text-only keyboard and timing check.
- `02-windows-flag-template`: the home-lab `Documents/flag1.txt` collection
  pattern. Its destination is deliberately `X:` and must be changed to the
  actual mounted Reconclave drive before it can work.
- `03-linux-info-demo`: opens a Linux terminal and prints a benign system
  summary.

Examples never replace an existing library and are never armed automatically.

## Loot and mass storage

The control deck can create, inspect, and remove named loot/log records up to
64 KiB each. This provides a persistent namespace for operator notes and future
capability output; it does not automatically collect arbitrary host files.

With a FAT-formatted microSD inserted at boot, the firmware now exposes the
card as a raw-block USB MSC volume alongside HID and CDC. While MSC media is
present, the host exclusively owns the SD card; firmware payloads remain on
LittleFS and the SD-backup API refuses to write. After the host ejects the
volume, firmware records the eject and may write a backup to `/reconclave`.

Because the host mounts the SD filesystem directly, files copied by a payload
are visible on the card without a firmware import step. Unplug only after the
host has flushed or safely ejected the volume.

## Architecture and safety

`App` owns USB, radio, node, storage, display, input, configuration, LED, and
trigger policy. A local payload runs from the button or the explicitly enabled
USB attach trigger. Hosted direct commands require the enrolled device credential;
LAN fleet execution requires HMAC authentication and verified signed scope.
Timer and unknown trigger sources are rejected. Names allow only 1–40
letters, digits, hyphens, or underscores; bodies and files have size limits.

## Capability matrix

| Capability | State |
| --- | --- |
| Composite USB HID keyboard + CDC | Implemented |
| SD-backed composite USB mass storage | Implemented; FAT card required |
| Local DuckyScript library and physical execution | Implemented |
| Persistent loot namespace | Implemented |
| Bounded persistent management/execution audit trail | Implemented |
| Payload syntax validation with line-level errors | Implemented |
| Optional microSD detection and one-tap backup | Implemented |
| Wi-Fi discovery and live device diagnostics | Implemented |
| Zeta-theme LCD dashboard | Implemented |
| AP+STA responsive control deck | Implemented |
| Reconclave announce, HMAC verification, replay protection | Implemented |
| Signed hosted HID scope delegation | Tested on ZetaD-Test3 with Test-Instance and this Linux workstation; broader device coverage untested |
| Signed trusted responses | Implemented and verified with Command |
| Encrypted evidence | Pending |
| Raw HID host-helper channel | Feasible |
| BLE observation / BLE HID | Feasible, memory-budget dependent |
| USB Ethernet gadget | High effort; lower-level TinyUSB/ESP-IDF work |

## Build

Provisioned collective builds require an ignored `src/generated_trust.h` with
the `rc_provisioned_peer_t` entries authorised for this dongle. The zero-key
file under `config/` is strictly a compile fixture and must never be treated as
fleet trust — flashing it leaves LAN fleet commands fail-closed. An enrolled
device can still use the separate hosted direct command queue.

```sh
pio run
```

### Flash the private build without wiping Wi-Fi/enrollment

Wi-Fi credentials and the hosted instance enrollment live in NVS (via
`Preferences`); payloads live in LittleFS. The trust key is compiled into the
app. So to swap a provisioned HID build onto an already-set-up dongle **without
re-provisioning**, flash without erasing — `pio run -t upload` writes the
bootloader, partition table, OTA selector, and app but leaves NVS and LittleFS
intact:

```sh
pio run -t upload                     # no erase: keeps Wi-Fi, enrollment, payloads
```

App-only alternative (same effect, explicit offset):

```sh
esptool --chip esp32s3 --port /dev/ttyACM0 --after hard_reset \
  write_flash 0x10000 .pio/build/t_dongle_s3/firmware.bin
```

Never pass `--erase-all` / "erase device": a full erase wipes NVS and LittleFS,
so Wi-Fi, enrollment, and payloads are lost and the dongle must be
re-provisioned. (A full erase also bricks unless the flash includes the
bootloader and partition table — see the hosted service's update manifest.)

Pushing a version tag (`vX.Y.Z`) runs [release.yml](.github/workflows/release.yml),
which builds against that same zero-key stand-in and publishes it as a GitHub
Release — an **unprovisioned example build** you flash and then provision for
real, not a deployable trusted LAN fleet node. It can still enroll in a hosted
instance for direct HID. See
[docs/releasing.md](https://github.com/Zetascrub/Reconclave/blob/main/docs/releasing.md#public-example-firmware-dummy-trust-ci-builds).

Current footprint is approximately 18% RAM and 15% application flash. Bench
verification is still required for display orientation, persistence, AP/STA
coexistence, USB enumeration, button execution, and retained state.
