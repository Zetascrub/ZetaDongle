#include "ui_service.h"

#include <Arduino.h>

#include "framework.h"  // pins
#include "logo.h"

namespace reconclave {
namespace {
constexpr unsigned long kBlinkIntervalMs = 500;
}  // namespace

void UiService::begin() {
  // LED first: an unambiguous "firmware got this far" signal even if the panel
  // or a later init hangs.
  FastLED.addLeds<APA102, pins::kLedData, pins::kLedClock, BGR>(led_, 1);
  FastLED.setBrightness(60);
  led_[0] = CRGB::Blue;
  FastLED.show();

  pinMode(pins::kBacklight, OUTPUT);
  digitalWrite(pins::kBacklight, LOW);  // active-low: LOW = backlight on
  tft_.init();
  tft_.setRotation(0);
  tft_.setSwapBytes(true);  // arrays are little-endian native; see logo.h
  tft_.pushImage(0, 0, kLogoWidth, kLogoHeight, kLogoImage);
  delay(1200);

  display_.begin(tft_);
  display_.playBootAnimation();
}

void UiService::flash(const CRGB& color, unsigned long ms) {
  flash_color_ = color;
  flash_until_ = millis() + ms;
}

void UiService::flashFired(uint32_t fire_count) {
  display_.flashFired(fire_count);
  flash(CRGB::Green, 200);
}

void UiService::updateDisplay(const ScriptStore& store, const String& ap_ssid,
                              const IPAddress& ap_ip) {
  display_.update(store, ap_ssid, ap_ip);
}

void UiService::tick(unsigned long now, bool button_pressed) {
  if (button_pressed) {
    led_[0] = CRGB::Red;
    FastLED.show();
  } else if (now < flash_until_) {
    led_[0] = flash_color_;
    FastLED.show();
  } else if (now >= next_blink_) {
    led_on_ = !led_on_;
    led_[0] = led_on_ ? CRGB::Blue : CRGB::Black;
    FastLED.show();
    next_blink_ = now + kBlinkIntervalMs;
  }
}

}  // namespace reconclave
