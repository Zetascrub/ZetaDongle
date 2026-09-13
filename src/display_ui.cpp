#include "display_ui.h"

#include "mascot_assets.h"

namespace reconclave {
namespace {
constexpr int kPageCount = 4;
constexpr unsigned long kPageIntervalMs = 3500;
constexpr unsigned long kFlashDurationMs = 1200;
constexpr unsigned long kBootFrameIntervalMs = 90;
constexpr int kBootLoops = 2;
}  // namespace

void DisplayUi::begin(TFT_eSPI& tft) {
  tft_ = &tft;
  // Brand navy from the Zetascrub palette - matches the flattened background
  // already baked into mascot_assets.h's icon/boot-frame art, so those blit
  // in with no visible box around them.
  bg_color_ = tft_->color565(0x08, 0x28, 0x3E);
}

void DisplayUi::playBootAnimation() {
  tft_->fillScreen(bg_color_);
  const int x = (80 - kBootFrameWidth) / 2;
  const int y = (160 - kBootFrameHeight) / 2 - 10;
  for (int loop = 0; loop < kBootLoops; loop++) {
    for (int i = 0; i < kBootFrameCount; i++) {
      tft_->pushImage(x, y, kBootFrameWidth, kBootFrameHeight, kBootFrames[i]);
      delay(kBootFrameIntervalMs);
    }
  }
  const uint16_t cyan = tft_->color565(0x22, 0xE0, 0xF2);
  tft_->setTextColor(cyan, bg_color_);
  tft_->setTextSize(1);
  tft_->setCursor(10, y + kBootFrameHeight + 10);
  tft_->print("ZETASCRUB");
  delay(500);
}

void DisplayUi::update(const ScriptStore& store, const String& ap_ssid,
                        const IPAddress& ap_ip) {
  const unsigned long now = millis();
  if (flash_until_ != 0) {
    if (now < flash_until_) return;
    flash_until_ = 0;
    last_drawn_page_ = -1;  // Force a redraw of the normal cycle on return.
  }
  if (now >= next_page_at_) {
    page_ = (page_ + 1) % kPageCount;
    next_page_at_ = now + kPageIntervalMs;
  }
  if (page_ != last_drawn_page_) {
    drawPage(page_, store, ap_ssid, ap_ip);
    last_drawn_page_ = page_;
  }
}

void DisplayUi::flashFired(uint32_t fire_count) {
  flash_until_ = millis() + kFlashDurationMs;
  const uint16_t green = tft_->color565(0x3A, 0xE0, 0x7A);
  tft_->fillScreen(bg_color_);
  tft_->setTextColor(green, bg_color_);
  tft_->setTextSize(2);
  tft_->setCursor(6, 60);
  tft_->print("FIRED!");
  tft_->setTextSize(1);
  tft_->setCursor(6, 90);
  tft_->print("count: ");
  tft_->print(fire_count);
}

void DisplayUi::drawPage(int page, const ScriptStore& store, const String& ap_ssid,
                          const IPAddress& ap_ip) {
  const uint16_t cyan = tft_->color565(0x22, 0xE0, 0xF2);
  const uint16_t tan = tft_->color565(0xB4, 0x83, 0x53);
  const uint16_t cream = tft_->color565(0xF8, 0xEE, 0xD0);
  const uint16_t orange = tft_->color565(0xF6, 0xA1, 0x02);

  tft_->fillScreen(bg_color_);
  tft_->setTextSize(1);

  switch (page) {
    case 0: {  // Brand / idle.
      tft_->pushImage((80 - kZetaIconWidth) / 2, 10, kZetaIconWidth, kZetaIconHeight,
                       kZetaIconImage);
      tft_->setTextColor(cyan, bg_color_);
      tft_->setCursor(10, 58);
      tft_->print("ZETASCRUB");
      tft_->setTextColor(cream, bg_color_);
      tft_->setCursor(4, 72);
      tft_->print("T-DONGLE-S3");
      tft_->setTextColor(tan, bg_color_);
      tft_->setCursor(4, 96);
      tft_->print("TINKER HACK");
      tft_->setCursor(4, 108);
      tft_->print("FLASH REPEAT");
      break;
    }
    case 1: {  // Network / web UI.
      tft_->setTextColor(cyan, bg_color_);
      tft_->setCursor(4, 14);
      tft_->print("WEB UI");
      tft_->setTextColor(cream, bg_color_);
      tft_->setCursor(4, 34);
      tft_->print(ap_ssid);
      tft_->setTextColor(tan, bg_color_);
      tft_->setCursor(4, 50);
      tft_->print("pass:");
      tft_->setCursor(4, 62);
      tft_->print("TinkerHackFlash");
      tft_->setTextColor(orange, bg_color_);
      tft_->setCursor(4, 90);
      tft_->print("http://");
      tft_->setCursor(4, 102);
      tft_->print(ap_ip.toString());
      break;
    }
    case 2: {  // Armed script.
      tft_->setTextColor(cyan, bg_color_);
      tft_->setCursor(4, 14);
      tft_->print("ARMED");
      const String assigned = store.assignedName();
      if (assigned.length() == 0) {
        tft_->setTextColor(tan, bg_color_);
        tft_->setCursor(4, 40);
        tft_->print("(none)");
        tft_->setCursor(4, 56);
        tft_->print("use web UI");
        tft_->setCursor(4, 68);
        tft_->print("to assign");
      } else {
        tft_->setTextColor(orange, bg_color_);
        tft_->setCursor(4, 40);
        tft_->print(assigned);
        tft_->setTextColor(cream, bg_color_);
        tft_->setCursor(4, 60);
        tft_->print("button fires");
        tft_->setCursor(4, 72);
        tft_->print("this script");
      }
      break;
    }
    case 3: {  // Stats.
      tft_->setTextColor(cyan, bg_color_);
      tft_->setCursor(4, 14);
      tft_->print("STATS");
      tft_->setTextColor(cream, bg_color_);
      tft_->setCursor(4, 36);
      tft_->print("fires: ");
      tft_->print(store.fireCount());
      tft_->setCursor(4, 50);
      tft_->print("(this boot)");
      const unsigned long last_fired = store.lastFiredMs();
      tft_->setTextColor(tan, bg_color_);
      tft_->setCursor(4, 76);
      if (last_fired == 0) {
        tft_->print("not fired");
        tft_->setCursor(4, 88);
        tft_->print("yet");
      } else {
        tft_->print("last:");
        tft_->setCursor(4, 88);
        tft_->print((millis() - last_fired) / 1000);
        tft_->print("s ago");
      }
      break;
    }
  }
}

}  // namespace reconclave
