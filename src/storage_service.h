#pragma once

#include <Arduino.h>
#include <FS.h>
#include <driver/sdmmc_types.h>

#include <vector>

namespace reconclave {

class StorageService {
 public:
  bool begin();

  std::vector<String> listPayloads() const;
  bool loadPayload(const String& name, String& body) const;
  bool savePayload(const String& name, const String& body, String& error);
  bool removePayload(const String& name);

  std::vector<String> listLoot() const;
  bool loadLoot(const String& name, String& body) const;
  bool saveLoot(const String& name, const String& body, String& error);
  bool removeLoot(const String& name);

  void appendAudit(const String& event, const String& detail);
  String auditLog(size_t max_bytes = 16384) const;
  bool backupToSd(String& error);
  bool sdAvailable() const { return sd_ready_; }
  bool sdHostOwned() const { return sd_host_owned_; }
  void setSdHostOwned(bool owned) { sd_host_owned_ = owned; }
  uint32_t sdBlockCount() const;
  uint16_t sdBlockSize() const;
  int32_t readSd(uint32_t lba, uint32_t offset, void* buffer, uint32_t size);
  int32_t writeSd(uint32_t lba, uint32_t offset, const uint8_t* buffer, uint32_t size);
  uint64_t sdTotalBytes() const;
  uint64_t sdUsedBytes() const;

  const String& armedPayload() const { return armed_; }
  bool armPayload(const String& name);
  void recordRun(bool ok);
  uint32_t runCount() const { return run_count_; }
  uint32_t failureCount() const { return failure_count_; }
  unsigned long lastRunMs() const { return last_run_ms_; }
  size_t usedBytes() const;
  size_t totalBytes() const;

  static bool validName(const String& name);
  static constexpr size_t kMaxPayloadBytes = 8192;
  static constexpr size_t kMaxLootBytes = 65536;

 private:
  bool saveFile(const char* directory, const String& name, const String& extension,
                const String& body, size_t limit, String& error);
  bool loadFile(const char* directory, const String& name, const String& extension,
                String& body) const;
  bool removeFile(const char* directory, const String& name, const String& extension);
  std::vector<String> listFiles(const char* directory, const String& extension) const;
  String pathFor(const char* directory, const String& name, const String& extension) const;
  void loadMeta();
  void saveMeta() const;
  void seedExamples();

  bool ready_ = false;
  bool sd_ready_ = false;
  bool sd_host_owned_ = false;
  sdmmc_card_t* sd_card_ = nullptr;
  String armed_;
  uint32_t run_count_ = 0;
  uint32_t failure_count_ = 0;
  unsigned long last_run_ms_ = 0;
};

}  // namespace reconclave
