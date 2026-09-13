# T-Dongle-S3 hardware capability map

A development reference for planning future firmware. It records what the
ESP32-S3 / T-Dongle-S3 hardware *can* do (implemented, feasible, or hard),
oriented toward a HID / field-tool role. It is a capability inventory, not a
feature commitment — anything powerful here stays behind the project's
authorised-use posture (physical-button firing, no autonomous/silent paths,
used only against hardware you own or are authorised to test).

Facts marked "confirmed" were verified against the real unit (see `README.md`);
others are hardware/framework capabilities that would need implementing.

## The bounding box (what constrains everything)

- **Dual-core Xtensa LX7 @ 240 MHz, 512 KB internal SRAM, 16 MB flash** (flash
  confirmed via `esptool flash-id`).
- **PSRAM is variant-dependent** — the base T-Dongle-S3 has little or none.
  Verify your unit before assuming a large RAM budget.
- **One USB 2.0 OTG PHY, Full-Speed (12 Mbps).** Device *or* host, not both at
  once; the USB-A-plug form factor means it operates as a **device**.
- Practical implication: design for **small, sequential, bounded** operations —
  no buffering large files in RAM, TLS is expensive, only a couple of network
  services concurrently. This matches the project's existing "bounded
  everything" posture (capped script length/delays/runtime).

## USB device roles — the BadUSB core (all via native TinyUSB)

| Class | Enables | Status |
| :-- | :-- | :-- |
| **HID keyboard** | Keystroke injection (DuckyScript subset) | Implemented |
| **HID mouse / consumer / raw-HID** | Pointer control; **raw HID = a bidirectional data channel** to a cooperating host app (a quiet data pipe with no drive) | Feasible, not built |
| **MSC (mass storage)** | Present a fake USB drive → Bash-Bunny-style file staging / exfiltration; back it with SD or flash | Feasible, not built |
| **CDC serial** | Serial gadget / console / data channel | Used for logging |
| **Composite (HID + MSC + CDC)** | Keyboard *and* drive *and* serial simultaneously | Supported by TinyUSB |
| **USB Ethernet (RNDIS / ECM / NCM)** | Present as a USB NIC → become the host's network → captive portal, rogue DHCP/DNS, WPAD/PAC, cleartext capture | **Hardest path.** TinyUSB has the class, but Arduino-ESP32 does not expose it turnkey — likely needs raw ESP-IDF/TinyUSB. Each service must be hand-written natively (no Responder/impacket to "install"). |

The Bash Bunny's marquee network attacks (QuickCreds, LLMNR/NBT-NS poisoning)
work because it is a full ARM Linux box running standard tooling. This is a
microcontroller: the USB-NIC *interface* is reachable, but every service behind
it is a native C reimplementation bounded by the RAM/CPU above.

## Radios — where this beats a (classic) Bash Bunny

- **Wi-Fi 2.4 GHz b/g/n:** SoftAP (already used for the web UI), STA, scanning,
  promiscuous-mode sniffing, and raw-frame TX (deauth, beacon/karma,
  evil-twin / captive portal).
- **BLE 5:** scan, advertise/spoof, **HID-over-BLE** (it can be a *wireless*
  keyboard, not only a USB one), BLE advertising spam.

A classic Bash Bunny has no radio at all; this is a whole native attack/recon
surface unique to the platform.

## Storage & local I/O (confirmed hardware)

- **16 MB flash** — LittleFS today uses a ~3–4 MB partition for scripts.
- **SD card slot** — SD_MMC 4-bit mode (CLK 12, CMD 16, D0 14, D1 17, D2 21,
  D3 18); GB-scale capacity for payloads / captured data.
- **ST7735 80×160 LCD**, **APA102 RGB LED** (data GPIO40, clock GPIO39),
  **GPIO0 button** — local status and control without a host.

## Design directions that play to the strengths

These lean on what the hardware does that a Bash Bunny cannot, rather than
imitating the Bunny directly:

1. **Exfiltration over Wi-Fi instead of mass storage.** A HID payload sends
   collected data to an operator-controlled endpoint over the device's own
   Wi-Fi — no physical retrieval needed, arguably more capable than the Bunny's
   drive-swap model.
2. **Raw-HID data channel.** A bidirectional link to a host-side helper without
   ever presenting mass storage or a NIC — smaller blast radius, still a real
   data path.
3. **Wireless HID (BLE keyboard).** Injection without a physical USB connection.

## Gotchas to design around

- Full-Speed USB (12 Mbps) caps throughput vs. the Bunny's Hi-Speed.
- `USBHIDKeyboard` sends **US-layout** keycodes regardless of the host's actual
  layout (already observed — `#` rendered as `£` on a UK host).
- Raw 802.11 TX (deauth/beacon) and any network-interception role are powerful
  and jurisdiction-sensitive — keep them behind explicit authorised-use gating.
- **No auto-reset circuit** on this board — flashing needs the manual
  hold-button / replug dance documented in `README.md`.
- Under `ARDUINO_USB_CDC_ON_BOOT=1`, `USB.begin()` runs before `setup()`, so USB
  descriptor strings must be set as compile-time build flags, not at runtime.
