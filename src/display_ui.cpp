#include "display_ui.h"

#include "radio_manager.h"
#include "reconclave/identity.h"
#include "storage_service.h"

namespace reconclave {
namespace {
constexpr int kPages = 4;
constexpr unsigned long kPageMs = 4000;
uint16_t c565(uint32_t c) {
  return static_cast<uint16_t>(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f));
}
const uint16_t kCanvas = c565(identity::kColorCanvas);
const uint16_t kSurface = c565(identity::kColorSurface);
const uint16_t kAccent = c565(identity::kColorAccent);
const uint16_t kWarning = c565(identity::kColorWarning);
const uint16_t kDanger = c565(identity::kColorDanger);
const uint16_t kInk = c565(identity::kColorInk);
const uint16_t kMuted = c565(identity::kColorInkMuted);
const uint16_t kBorder = c565(identity::kColorBorder);
}  // namespace

void DisplayUi::begin() {
  pinMode(38, OUTPUT);
  digitalWrite(38, LOW);
  display_.init();
  display_.setRotation(0);
  display_.setTextWrap(false);
  display_.fillScreen(kCanvas);
  display_.drawCircle(40, 55, 25, kAccent);
  display_.drawCircle(40, 55, 21, kBorder);
  display_.setTextColor(kAccent, kCanvas);
  display_.setTextSize(3);
  display_.setCursor(31, 44);
  display_.print("Z");
  display_.setTextSize(1);
  display_.setTextColor(kInk, kCanvas);
  display_.setCursor(7, 94);
  display_.print("RECONCLAVE");
  display_.setTextColor(kMuted, kCanvas);
  display_.setCursor(10, 108);
  display_.print("T-DONGLE");
  delay(900);
  next_page_ms_ = millis() + kPageMs;
}

void DisplayUi::drawFrame(const char* title, uint16_t state_color) {
  display_.fillScreen(kCanvas);
  display_.fillRect(0, 0, 80, 18, kSurface);
  display_.drawFastHLine(0, 18, 80, kBorder);
  display_.fillRect(0, 19, 2, 123, state_color);
  display_.setTextSize(1);
  display_.setTextColor(kInk, kSurface);
  display_.setCursor(5, 5);
  display_.print(title);
  display_.setTextColor(kMuted, kCanvas);
  display_.setCursor(4, 149);
  display_.printf("%d/%d", page_ + 1, kPages);
}

void DisplayUi::printClipped(const String& text, int x, int y, int chars, uint16_t color) {
  String shown = text;
  if (shown.length() > static_cast<size_t>(chars)) shown = shown.substring(0, chars - 1) + "~";
  display_.setTextColor(color, kCanvas);
  display_.setCursor(x, y);
  display_.print(shown);
}

void DisplayUi::drawPage(const StorageService& storage, const RadioManager& radio,
                         bool node_linked) {
  if (page_ == 0) {
    drawFrame("DONGLE", node_linked ? kAccent : kWarning);
    display_.setTextColor(node_linked ? kAccent : kWarning, kCanvas);
    display_.setCursor(7, 31); display_.print(node_linked ? "NODE LINK" : "STANDALONE");
    display_.setTextColor(kInk, kCanvas);
    display_.setCursor(7, 52); display_.print("READY");
    display_.setTextColor(kMuted, kCanvas);
    display_.setCursor(7, 75); display_.print("AP");
    printClipped(radio.apSsid(), 7, 88, 11, kInk);
    printClipped(radio.apIp().toString(), 7, 102, 13, kAccent);
  } else if (page_ == 1) {
    const bool armed = storage.armedPayload().length() > 0;
    drawFrame("PAYLOAD", armed ? kWarning : kMuted);
    display_.setTextColor(armed ? kWarning : kMuted, kCanvas);
    display_.setCursor(7, 31); display_.print(armed ? "ARMED" : "SAFE");
    printClipped(armed ? storage.armedPayload() : "none", 7, 52, 11, kInk);
    display_.setTextColor(kMuted, kCanvas);
    display_.setCursor(7, 78); display_.print(armed ? "PRESS BUTTON" : "ARM IN WEB UI");
    display_.setCursor(7, 92); display_.printf("%u saved", static_cast<unsigned>(storage.listPayloads().size()));
  } else if (page_ == 2) {
    drawFrame("STORAGE", kAccent);
    display_.setTextColor(kInk, kCanvas);
    display_.setCursor(7, 31); display_.printf("LOOT %u", static_cast<unsigned>(storage.listLoot().size()));
    display_.setCursor(7, 50); display_.printf("USED %uK", static_cast<unsigned>(storage.usedBytes() / 1024));
    display_.setTextColor(kMuted, kCanvas);
    display_.setCursor(7, 70); display_.printf("FREE %uK", static_cast<unsigned>((storage.totalBytes() - storage.usedBytes()) / 1024));
    display_.setCursor(7, 94); display_.print("MSC PLANNED");
  } else {
    drawFrame("ACTIVITY", storage.failureCount() ? kWarning : kAccent);
    display_.setTextColor(kInk, kCanvas);
    display_.setCursor(7, 31); display_.printf("RUNS %u", static_cast<unsigned>(storage.runCount()));
    display_.setCursor(7, 49); display_.printf("FAIL %u", static_cast<unsigned>(storage.failureCount()));
    display_.setTextColor(kMuted, kCanvas);
    display_.setCursor(7, 73); display_.print(node_linked ? "FLEET ONLINE" : "FLEET OFFLINE");
    display_.setCursor(7, 87); display_.print(radio.staConnected() ? "STA CONNECTED" : "AP ONLY");
  }
}

void DisplayUi::update(const StorageService& storage, const RadioManager& radio, bool node_linked) {
  const unsigned long now = millis();
  if (overlay_until_ms_ && now < overlay_until_ms_) return;
  if (overlay_until_ms_) { overlay_until_ms_ = 0; drawn_page_ = -1; }
  if (now >= next_page_ms_) {
    page_ = (page_ + 1) % kPages;
    next_page_ms_ = now + kPageMs;
  }
  if (drawn_page_ != page_) {
    drawPage(storage, radio, node_linked);
    drawn_page_ = page_;
  }
}

void DisplayUi::showRunResult(const String& payload, bool ok, const String& detail) {
  drawFrame(ok ? "COMPLETE" : "FAILED", ok ? kAccent : kDanger);
  display_.setTextColor(ok ? kAccent : kDanger, kCanvas);
  display_.setCursor(7, 34); display_.print(ok ? "PAYLOAD DONE" : "PAYLOAD STOP");
  printClipped(payload, 7, 55, 11, kInk);
  printClipped(detail, 7, 78, 11, kMuted);
  overlay_until_ms_ = millis() + 1800;
}

}  // namespace reconclave
