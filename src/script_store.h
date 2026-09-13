// LittleFS-backed storage for named DuckyScript-subset scripts, plus which
// one (if any) is assigned to the physical button. Mounted on the 16MB
// partition table's spare "spiffs"-labeled data partition (~3.4MB) - see
// devices/t-dongle-s3/README.md.
#pragma once

#include <Arduino.h>

#include <vector>

namespace reconclave {

class ScriptStore {
 public:
  // Mounts LittleFS, formatting it on first boot / after a flash erase.
  bool begin();

  std::vector<String> listNames() const;
  bool load(const String& name, String& body_out) const;

  // Rejects invalid names (see validName()) or bodies over kMaxScriptBytes
  // without writing anything; error_out is set on failure.
  bool save(const String& name, const String& body, String& error_out);

  // Also clears the assignment if `name` was the assigned script.
  bool remove(const String& name);

  const String& assignedName() const { return assigned_; }
  // "" clears the assignment. Fails if `name` doesn't name an existing
  // script (still lets "" through so unassigning never fails).
  bool setAssigned(const String& name);

  uint32_t fireCount() const { return fire_count_; }
  // 0 = not fired yet since this boot (millis()-based, so it's meaningless -
  // and not persisted - across a reboot; see README).
  unsigned long lastFiredMs() const { return last_fired_ms_; }
  void recordFire();

  static bool validName(const String& name);
  static constexpr size_t kMaxScriptBytes = 8192;

 private:
  String scriptPath(const String& name) const;
  void loadMeta();
  void saveMeta() const;

  String assigned_;
  uint32_t fire_count_ = 0;
  unsigned long last_fired_ms_ = 0;
  bool began_ = false;
};

}  // namespace reconclave
