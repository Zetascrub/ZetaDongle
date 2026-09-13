// Owns on-device storage: the LittleFS-backed ScriptStore (unchanged), plus an
// optional SD card (SD_MMC 4-bit) for payloads/captured data that outgrow the
// ~3.4MB LittleFS partition. SD is best-effort: absent card => sdAvailable()
// false, everything else still works.
#pragma once

#include "script_store.h"

namespace reconclave {

class StorageService {
 public:
  bool begin();
  ScriptStore& scripts() { return scripts_; }
  bool sdAvailable() const { return sd_ok_; }

 private:
  ScriptStore scripts_;
  bool sd_ok_ = false;
};

}  // namespace reconclave
