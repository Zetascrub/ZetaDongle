#include "storage_service.h"

#include <Arduino.h>
#include <SD_MMC.h>

namespace reconclave {
namespace {
// SD_MMC 4-bit pins, confirmed against the real unit (README.md).
constexpr int kSdClk = 12, kSdCmd = 16, kSdD0 = 14, kSdD1 = 17, kSdD2 = 21, kSdD3 = 18;
}  // namespace

bool StorageService::begin() {
  const bool fs_ok = scripts_.begin();
  if (!fs_ok) Serial.println("storage: LittleFS mount failed - scripts will not persist");

  // SD is optional; a missing/unformatted card must not stop the device.
  SD_MMC.setPins(kSdClk, kSdCmd, kSdD0, kSdD1, kSdD2, kSdD3);
  sd_ok_ = SD_MMC.begin("/sdcard", /*mode1bit=*/false, /*format_if_mount_failed=*/false);
  Serial.printf("storage: SD %s\n", sd_ok_ ? "mounted" : "absent/unavailable");
  return fs_ok;
}

}  // namespace reconclave
