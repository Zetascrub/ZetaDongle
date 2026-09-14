#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

namespace reconclave {

class RadioManager;
class StorageService;

class DisplayUi {
 public:
  void begin();
  void update(const StorageService& storage, const RadioManager& radio, bool node_linked);
  void showRunResult(const String& payload, bool ok, const String& detail);

 private:
  void drawFrame(const char* title, uint16_t state_color);
  void drawPage(const StorageService& storage, const RadioManager& radio, bool node_linked);
  void printClipped(const String& text, int x, int y, int chars, uint16_t color);

  TFT_eSPI display_;
  int page_ = 0;
  int drawn_page_ = -1;
  unsigned long next_page_ms_ = 0;
  unsigned long overlay_until_ms_ = 0;
};

}  // namespace reconclave
