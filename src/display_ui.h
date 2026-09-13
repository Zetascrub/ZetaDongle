// Drives the ST7735: a one-shot boot animation, then an auto-cycling status
// dashboard (SSID/IP for the web UI, armed script, fire count) instead of a
// static logo. The physical button stays dedicated to firing the armed
// script (see main.cpp/README.md), so page-cycling here is timer-driven, not
// input-driven. Owns no button/network/script logic itself - main.cpp feeds
// it state each loop() and calls flashFired() right after a script runs.
#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <TFT_eSPI.h>

#include "script_store.h"

namespace reconclave {

class DisplayUi {
 public:
  void begin(TFT_eSPI& tft);

  // Blocking (~2s); call once from setup(), after begin().
  void playBootAnimation();

  // Cheap to call every loop() iteration - internally gated by millis(), only
  // touches the panel when the page actually changes or a flash is active.
  void update(const ScriptStore& store, const String& ap_ssid, const IPAddress& ap_ip);

  // Briefly overlays a "fired" confirmation, then returns to the normal
  // cycle. Called from the button-release path right after a script runs.
  void flashFired(uint32_t fire_count);

 private:
  void drawPage(int page, const ScriptStore& store, const String& ap_ssid,
                const IPAddress& ap_ip);

  TFT_eSPI* tft_ = nullptr;
  uint16_t bg_color_ = 0;
  int page_ = 0;
  int last_drawn_page_ = -1;
  unsigned long next_page_at_ = 0;
  unsigned long flash_until_ = 0;
};

}  // namespace reconclave
