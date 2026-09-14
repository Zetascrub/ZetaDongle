#include "storage_service.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

#include <algorithm>

namespace reconclave {
namespace {
constexpr const char* kPayloadDir = "/payloads";
constexpr const char* kLootDir = "/loot";
constexpr const char* kMetaPath = "/device.json";
constexpr const char* kAuditPath = "/audit.log";
constexpr size_t kMaxAuditBytes = 32768;
constexpr int kSdClk = 12;
constexpr int kSdCmd = 16;
constexpr int kSdD0 = 14;
constexpr int kSdD1 = 17;
constexpr int kSdD2 = 21;
constexpr int kSdD3 = 18;
}  // namespace

bool StorageService::validName(const String& name) {
  if (name.length() == 0 || name.length() > 40 || name == "." || name == "..") return false;
  for (size_t i = 0; i < name.length(); ++i) {
    const char c = name[i];
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) return false;
  }
  return true;
}

String StorageService::pathFor(const char* directory, const String& name,
                               const String& extension) const {
  return String(directory) + "/" + name + extension;
}

bool StorageService::begin() {
  ready_ = LittleFS.begin(/*formatOnFail=*/true);
  if (!ready_) {
    Serial.println("storage: LittleFS mount failed");
    return false;
  }
  if (!LittleFS.exists(kPayloadDir)) LittleFS.mkdir(kPayloadDir);
  if (!LittleFS.exists(kLootDir)) LittleFS.mkdir(kLootDir);
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_4BIT;
  host.slot = SDMMC_HOST_SLOT_1;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk = static_cast<gpio_num_t>(kSdClk);
  slot.cmd = static_cast<gpio_num_t>(kSdCmd);
  slot.d0 = static_cast<gpio_num_t>(kSdD0);
  slot.d1 = static_cast<gpio_num_t>(kSdD1);
  slot.d2 = static_cast<gpio_num_t>(kSdD2);
  slot.d3 = static_cast<gpio_num_t>(kSdD3);
  slot.width = 4;
  esp_vfs_fat_sdmmc_mount_config_t mount = {};
  mount.format_if_mount_failed = false;
  mount.max_files = 5;
  mount.allocation_unit_size = 0;
  sd_ready_ = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount, &sd_card_) == ESP_OK;
  loadMeta();
  seedExamples();
  Serial.printf("storage: %u/%u bytes, armed=\"%s\"\n",
                static_cast<unsigned>(usedBytes()), static_cast<unsigned>(totalBytes()),
                armed_.c_str());
  Serial.printf("storage: SD %s\n", sd_ready_ ? "mounted" : "absent");
  appendAudit("boot", sd_ready_ ? "littlefs+sd" : "littlefs");
  return true;
}

void StorageService::seedExamples() {
  if (!listPayloads().empty()) return;
  String error;
  savePayload("01-hello-safe",
              "REM Safe editor and keyboard test - types text only\n"
              "DELAY 1200\n"
              "STRINGLN Reconclave Dongle ready\n",
              error);
  savePayload("02-windows-flag-template",
              "REM HOME-LAB TEMPLATE - edit X: to the mounted Reconclave drive\n"
              "REM Confirm the destination before arming; this is intentionally unarmed\n"
              "DELAY 2500\n"
              "GUI r\n"
              "DELAY 500\n"
              "STRING powershell -NoProfile -Command \"Copy-Item -LiteralPath "
              "'$env:USERPROFILE\\Documents\\flag1.txt' -Destination 'X:\\flag1.txt'\"\n"
              "ENTER\n",
              error);
  savePayload("03-linux-info-demo",
              "REM Opens a terminal and prints a benign system summary\n"
              "DELAY 1800\n"
              "CTRL ALT t\n"
              "DELAY 700\n"
              "STRINGLN uname -a\n",
              error);
  appendAudit("examples-seeded", "3 payloads, none armed");
}

std::vector<String> StorageService::listFiles(const char* directory,
                                              const String& extension) const {
  std::vector<String> result;
  if (!ready_) return result;
  File dir = LittleFS.open(directory);
  if (!dir || !dir.isDirectory()) return result;
  for (File file = dir.openNextFile(); file; file = dir.openNextFile()) {
    String name = file.name();
    if (name.endsWith(extension)) name.remove(name.length() - extension.length());
    if (validName(name)) result.push_back(name);
  }
  std::sort(result.begin(), result.end(), [](const String& a, const String& b) {
    return a.compareTo(b) < 0;
  });
  return result;
}

bool StorageService::loadFile(const char* directory, const String& name,
                              const String& extension, String& body) const {
  if (!ready_ || !validName(name)) return false;
  File file = LittleFS.open(pathFor(directory, name, extension), "r");
  if (!file) return false;
  body = file.readString();
  file.close();
  return true;
}

bool StorageService::saveFile(const char* directory, const String& name,
                              const String& extension, const String& body, size_t limit,
                              String& error) {
  if (!ready_) { error = "storage unavailable"; return false; }
  if (!validName(name)) { error = "name must use 1-40 letters, digits, - or _"; return false; }
  if (body.length() > limit) { error = "file exceeds " + String(limit) + " bytes"; return false; }
  File file = LittleFS.open(pathFor(directory, name, extension), "w");
  if (!file) { error = "could not open file"; return false; }
  const size_t written = file.print(body);
  file.close();
  if (written != body.length()) { error = "short write"; return false; }
  return true;
}

bool StorageService::removeFile(const char* directory, const String& name,
                                const String& extension) {
  return ready_ && validName(name) && LittleFS.remove(pathFor(directory, name, extension));
}

std::vector<String> StorageService::listPayloads() const { return listFiles(kPayloadDir, ".txt"); }
bool StorageService::loadPayload(const String& n, String& b) const { return loadFile(kPayloadDir, n, ".txt", b); }
bool StorageService::savePayload(const String& n, const String& b, String& e) { return saveFile(kPayloadDir, n, ".txt", b, kMaxPayloadBytes, e); }
bool StorageService::removePayload(const String& n) {
  const bool removed = removeFile(kPayloadDir, n, ".txt");
  if (removed && armed_ == n) { armed_ = ""; saveMeta(); }
  return removed;
}
std::vector<String> StorageService::listLoot() const { return listFiles(kLootDir, ".log"); }
bool StorageService::loadLoot(const String& n, String& b) const { return loadFile(kLootDir, n, ".log", b); }
bool StorageService::saveLoot(const String& n, const String& b, String& e) { return saveFile(kLootDir, n, ".log", b, kMaxLootBytes, e); }
bool StorageService::removeLoot(const String& n) { return removeFile(kLootDir, n, ".log"); }

bool StorageService::armPayload(const String& name) {
  if (name.length() && (!validName(name) || !LittleFS.exists(pathFor(kPayloadDir, name, ".txt")))) return false;
  armed_ = name;
  saveMeta();
  return true;
}

void StorageService::recordRun(bool ok) {
  ++run_count_;
  if (!ok) ++failure_count_;
  last_run_ms_ = millis();
  saveMeta();
}

void StorageService::appendAudit(const String& event, const String& detail) {
  if (!ready_) return;
  File current = LittleFS.open(kAuditPath, "r");
  String tail;
  if (current && current.size() > kMaxAuditBytes) {
    current.seek(current.size() - (kMaxAuditBytes / 2));
    tail = current.readString();
    const int newline = tail.indexOf('\n');
    if (newline >= 0) tail.remove(0, newline + 1);
  }
  if (current) current.close();
  if (tail.length()) {
    File compact = LittleFS.open(kAuditPath, "w");
    if (compact) { compact.print(tail); compact.close(); }
  }
  File file = LittleFS.open(kAuditPath, "a");
  if (!file) return;
  file.printf("%lu\t%s\t%s\n", static_cast<unsigned long>(millis()), event.c_str(),
              detail.c_str());
  file.close();
}

String StorageService::auditLog(size_t max_bytes) const {
  if (!ready_) return String();
  File file = LittleFS.open(kAuditPath, "r");
  if (!file) return String();
  if (file.size() > max_bytes) file.seek(file.size() - max_bytes);
  String out = file.readString();
  file.close();
  return out;
}

bool StorageService::backupToSd(String& error) {
  if (!sd_ready_) { error = "SD card unavailable"; return false; }
  if (sd_host_owned_) { error = "eject Reconclave SD from the host first"; return false; }
  mkdir("/sdcard/reconclave", 0755);
  mkdir("/sdcard/reconclave/payloads", 0755);
  mkdir("/sdcard/reconclave/loot", 0755);
  auto copyGroup = [&](const std::vector<String>& names, const char* source_dir,
                       const char* target_dir, const char* extension) {
    for (const String& name : names) {
      File source = LittleFS.open(pathFor(source_dir, name, extension), "r");
      const String target_path = String("/sdcard") + target_dir + "/" + name + extension;
      FILE* target = fopen(target_path.c_str(), "wb");
      if (!source || target == nullptr) { if (source) source.close(); if (target) fclose(target); return false; }
      uint8_t buffer[512];
      while (source.available()) {
        const size_t count = source.read(buffer, sizeof(buffer));
        if (fwrite(buffer, 1, count, target) != count) { source.close(); fclose(target); return false; }
      }
      source.close(); fflush(target); fclose(target);
    }
    return true;
  };
  if (!copyGroup(listPayloads(), kPayloadDir, "/reconclave/payloads", ".txt") ||
      !copyGroup(listLoot(), kLootDir, "/reconclave/loot", ".log")) {
    error = "SD backup write failed"; return false;
  }
  FILE* audit = fopen("/sdcard/reconclave/audit.log", "wb");
  if (audit) { const String log = auditLog(kMaxAuditBytes); fwrite(log.c_str(), 1, log.length(), audit); fclose(audit); }
  appendAudit("backup", "sd");
  return true;
}

uint64_t StorageService::sdTotalBytes() const {
  return sd_ready_ && sd_card_ ? static_cast<uint64_t>(sd_card_->csd.capacity) * sd_card_->csd.sector_size : 0;
}
uint64_t StorageService::sdUsedBytes() const { return 0; }

uint32_t StorageService::sdBlockCount() const {
  return sd_ready_ && sd_card_ ? sd_card_->csd.capacity : 0;
}
uint16_t StorageService::sdBlockSize() const {
  return sd_ready_ && sd_card_ ? sd_card_->csd.sector_size : 512;
}

int32_t StorageService::readSd(uint32_t lba, uint32_t offset, void* buffer, uint32_t size) {
  if (!sd_ready_ || !sd_card_ || !sd_host_owned_) return -1;
  const uint32_t block = sdBlockSize();
  if (offset == 0 && size % block == 0) {
    return sdmmc_read_sectors(sd_card_, buffer, lba, size / block) == ESP_OK ? size : -1;
  }
  uint8_t scratch[512];
  uint8_t* output = static_cast<uint8_t*>(buffer);
  uint32_t done = 0;
  while (done < size) {
    const uint32_t sector = lba + (offset + done) / block;
    const uint32_t within = (offset + done) % block;
    const uint32_t chunk = std::min(block - within, size - done);
    if (sdmmc_read_sectors(sd_card_, scratch, sector, 1) != ESP_OK) return -1;
    memcpy(output + done, scratch + within, chunk);
    done += chunk;
  }
  return size;
}

int32_t StorageService::writeSd(uint32_t lba, uint32_t offset, const uint8_t* buffer,
                                uint32_t size) {
  if (!sd_ready_ || !sd_card_ || !sd_host_owned_) return -1;
  const uint32_t block = sdBlockSize();
  if (offset == 0 && size % block == 0) {
    return sdmmc_write_sectors(sd_card_, buffer, lba, size / block) == ESP_OK ? size : -1;
  }
  uint8_t scratch[512];
  uint32_t done = 0;
  while (done < size) {
    const uint32_t sector = lba + (offset + done) / block;
    const uint32_t within = (offset + done) % block;
    const uint32_t chunk = std::min(block - within, size - done);
    if (sdmmc_read_sectors(sd_card_, scratch, sector, 1) != ESP_OK) return -1;
    memcpy(scratch + within, buffer + done, chunk);
    if (sdmmc_write_sectors(sd_card_, scratch, sector, 1) != ESP_OK) return -1;
    done += chunk;
  }
  return size;
}

void StorageService::loadMeta() {
  File file = LittleFS.open(kMetaPath, "r");
  if (!file) return;
  JsonDocument doc;
  if (!deserializeJson(doc, file)) {
    armed_ = doc["armed"] | "";
    run_count_ = doc["runs"] | 0;
    failure_count_ = doc["failures"] | 0;
  }
  file.close();
  if (armed_.length() && !LittleFS.exists(pathFor(kPayloadDir, armed_, ".txt"))) armed_ = "";
}

void StorageService::saveMeta() const {
  if (!ready_) return;
  File file = LittleFS.open(kMetaPath, "w");
  if (!file) return;
  JsonDocument doc;
  doc["armed"] = armed_;
  doc["runs"] = run_count_;
  doc["failures"] = failure_count_;
  serializeJson(doc, file);
  file.close();
}

size_t StorageService::usedBytes() const { return ready_ ? LittleFS.usedBytes() : 0; }
size_t StorageService::totalBytes() const { return ready_ ? LittleFS.totalBytes() : 0; }

}  // namespace reconclave
