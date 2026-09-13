// T-Dongle-S3 milestone 4: Zeta-themed web script editor + auto-cycling
// display dashboard, on top of milestones 2+3's USB HID keyboard and ST7735
// display.
//
// What changed from milestone 2+3's fixed, inert test string: the device now
// runs a standalone WiFi AP (see web_ui.cpp) serving a script editor
// (web_ui_page.h) where DuckyScript-subset payloads (ducky_script.h/.cpp)
// can be written, saved, and assigned to the physical button
// (script_store.h/.cpp, on LittleFS). What deliberately has NOT changed: the
// physical button is still the *only* thing that can make a keystroke
// happen. The web UI can edit and arm scripts but has no "run" endpoint at
// all - firing a script over the network, even from this device's own AP,
// would be exactly the "message arrives, keys get typed" remote-trigger path
// the project constraint (see README.md and devices/k230's trust_policy.cpp)
// says must go through a signed-scope/evidence-logging trust model before it
// can exist, and that model isn't built yet. So: no network path to a
// keystroke exists here, full stop, same as milestone 2+3 - there's just
// now a nicer way to author what the button does.
//
// ducky_script.h's own bounded limits (max lines, max single/total delay,
// max REPEAT count) are a second, independent layer on top of that: they
// bound what a *legitimate* button press can do, they are not themselves why
// remote firing doesn't exist.
#include <Arduino.h>
#include <FastLED.h>
#include <TFT_eSPI.h>
#include <USB.h>
#include <USBHIDKeyboard.h>

#include "display_ui.h"
#include "ducky_script.h"
#include "logo.h"
#include "script_store.h"
#include "web_ui.h"

namespace {

// ST7735 80x160, backlight on GPIO38 - active-LOW (0=on, 1=off), confirmed
// from LILYGO's own examples/TFT_eSPI/TFT_eSPI.ino and examples/lcd/lcd.ino
// (both drive it manually rather than through TFT_eSPI's own TFT_BL macro,
// so this does the same). All other display pins/timing come from Bodmer/
// TFT_eSPI's own bundled Setup209_LilyGo_T_Dongle_S3.h, applied via build
// flags in platformio.ini.
constexpr int kBacklightPin = 38;
TFT_eSPI g_tft;

// APA102 (data+clock, NOT WS2812's single-wire protocol) - confirmed from
// Xinyuan-LilyGO/T-Dongle-S3's own examples/led/led.ino, not assumed from
// the single-pin "RGB LED" mentions some third-party pinout pages give.
constexpr int kLedDataPin = 40;
constexpr int kLedClockPin = 39;
CRGB g_led[1];

// See milestone 1's main.cpp history for why this is hardcoded rather than
// a `BOOT_PIN` macro: that macro comes from a LILYGO board-variant header
// PlatformIO doesn't have for this unlisted board.
constexpr int kButtonPin = 0;
constexpr unsigned long kBlinkIntervalMs = 500;
constexpr unsigned long kFiredFlashMs = 200;
constexpr unsigned long kNoScriptFlashMs = 200;
constexpr unsigned long kHeartbeatIntervalMs = 5000;

USBHIDKeyboard g_keyboard;
reconclave::ScriptStore g_store;
reconclave::WebUi g_web;
reconclave::DisplayUi g_display;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);  // Let the USB CDC side actually enumerate before printing.
  Serial.println();
  Serial.println("reconclave t-dongle-s3: milestone 4 (web script editor)");

  pinMode(kButtonPin, INPUT_PULLUP);

  // LED first, before anything display/network-related: an unambiguous,
  // host-independent "firmware got this far" signal in case something below
  // hangs.
  FastLED.addLeds<APA102, kLedDataPin, kLedClockPin, BGR>(g_led, 1);
  FastLED.setBrightness(60);
  g_led[0] = CRGB::Blue;
  FastLED.show();

  pinMode(kBacklightPin, OUTPUT);
  digitalWrite(kBacklightPin, LOW);  // Active-low: LOW = backlight on.
  g_tft.init();
  g_tft.setRotation(0);  // Portrait, matching the art's native 80x160/64x98.
  // pushImage() expects big-endian pixel words by default; our generated
  // arrays are plain little-endian uint16_t (native ESP32 byte order) - see
  // logo.h/mascot_assets.h. Confirmed empirically in milestone 2+3: without
  // this, cyan rendered as yellow.
  g_tft.setSwapBytes(true);
  g_tft.pushImage(0, 0, kLogoWidth, kLogoHeight, kLogoImage);
  delay(1200);

  g_display.begin(g_tft);
  g_display.playBootAnimation();

  if (!g_store.begin()) {
    Serial.println("script_store: mount failed - scripts will not persist");
  }
  g_web.begin(g_store);

  // Device identity ("Reconclave"/"Reconclave T-Dongle-S3" - not spoofed as
  // any particular commercial keyboard) is set via USB_MANUFACTURER/
  // USB_PRODUCT build flags in platformio.ini, not here: ARDUINO_USB_CDC_
  // ON_BOOT already calls USB.begin() before setup() runs, so by now
  // USB.productName()/manufacturerName() would silently no-op.
  g_keyboard.begin();
}

void loop() {
  static unsigned long next_blink = 0;
  static unsigned long next_heartbeat = 0;
  static unsigned long flash_until = 0;
  static CRGB flash_color = CRGB::Black;
  static bool led_on = true;
  static bool last_button_state = true;  // INPUT_PULLUP: idle high.

  g_web.handleClient();

  const unsigned long now = millis();
  const bool button_pressed = digitalRead(kButtonPin) == LOW;

  if (button_pressed != !last_button_state) {
    last_button_state = !button_pressed;
    if (button_pressed) {
      Serial.println("button: down");
    } else {
      // Fire only on release, i.e. a completed press-and-release, not on
      // every tick the button happens to be held.
      const String assigned = g_store.assignedName();
      if (assigned.length() == 0) {
        Serial.println("button: up - no script assigned, nothing to do");
        flash_color = CRGB::Orange;  // Distinct from "fired": button worked,
        flash_until = now + kNoScriptFlashMs;  // but nothing is armed to run.
      } else {
        String body;
        if (!g_store.load(assigned, body)) {
          Serial.printf("button: up - assigned script \"%s\" failed to load\n",
                        assigned.c_str());
          flash_color = CRGB::Red;
          flash_until = now + kNoScriptFlashMs;
        } else {
          Serial.printf("button: up - running \"%s\"\n", assigned.c_str());
          const reconclave::DuckyScriptResult result =
              reconclave::runDuckyScript(body, g_keyboard);
          if (result.ok) {
            Serial.printf("  ok, %d step(s)\n", result.steps_run);
          } else {
            Serial.printf("  stopped at line %d: %s (%d step(s) ran)\n",
                          result.line_number, result.error.c_str(), result.steps_run);
          }
          g_store.recordFire();
          g_display.flashFired(g_store.fireCount());
          flash_color = CRGB::Green;
          flash_until = now + kFiredFlashMs;
        }
      }
    }
  }

  if (now >= next_heartbeat) {
    Serial.printf("heartbeat: alive, fires=%u, assigned=\"%s\", ap=%s\n",
                  static_cast<unsigned>(g_store.fireCount()), g_store.assignedName().c_str(),
                  g_web.apSsid().c_str());
    next_heartbeat = now + kHeartbeatIntervalMs;
  }

  g_display.update(g_store, g_web.apSsid(), g_web.apIp());

  if (button_pressed) {
    g_led[0] = CRGB::Red;
    FastLED.show();
  } else if (now < flash_until) {
    g_led[0] = flash_color;
    FastLED.show();
  } else if (now >= next_blink) {
    led_on = !led_on;
    g_led[0] = led_on ? CRGB::Blue : CRGB::Black;
    FastLED.show();
    next_blink = now + kBlinkIntervalMs;
  }
}
