// Owns the local human interface: the ST7735 panel (via DisplayUi) and the
// APA102 status LED. Absorbs the boot-logo/animation sequence and the LED
// state machine that used to live in main.cpp. Modules never touch the panel
// or LED directly — they call flash()/flashFired() and the App drives the
// per-loop tick and dashboard refresh.
#pragma once

#include <FastLED.h>
#include <IPAddress.h>
#include <TFT_eSPI.h>

#include "display_ui.h"
#include "script_store.h"

namespace reconclave {

class UiService {
 public:
  // Backlight + TFT + boot logo + boot animation + LED. Blocking (~2s) by
  // design; call once at startup.
  void begin();

  // LED state machine: pressed => red, else an active flash color, else the
  // idle blink. Call every loop.
  void tick(unsigned long now, bool button_pressed);

  // Cheap: internally gated, only touches the panel when the page changes.
  void updateDisplay(const ScriptStore& store, const String& ap_ssid, const IPAddress& ap_ip);

  // Transient LED flash (e.g. orange = nothing armed, red = load failed).
  void flash(const CRGB& color, unsigned long ms);

  // "Fired" confirmation on the panel plus a green LED flash.
  void flashFired(uint32_t fire_count);

 private:
  TFT_eSPI tft_;
  DisplayUi display_;
  CRGB led_[1];
  unsigned long flash_until_ = 0;
  unsigned long next_blink_ = 0;
  CRGB flash_color_ = CRGB::Black;
  bool led_on_ = true;
};

}  // namespace reconclave
