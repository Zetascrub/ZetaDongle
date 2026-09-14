#include "usb_manager.h"

#include <Arduino.h>

#include "storage_service.h"

namespace reconclave {

namespace {
StorageService* g_storage = nullptr;
int32_t onMscRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t size) {
  return g_storage ? g_storage->readSd(lba, offset, buffer, size) : -1;
}
int32_t onMscWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t size) {
  return g_storage ? g_storage->writeSd(lba, offset, buffer, size) : -1;
}
bool onMscStartStop(uint8_t, bool start, bool eject) {
  if (!g_storage) return false;
  if (eject && !start) {
    g_storage->setSdHostOwned(false);
    g_storage->appendAudit("msc-eject", "host released SD");
  } else if (start) {
    g_storage->setSdHostOwned(true);
  }
  return true;
}
}  // namespace

void UsbManager::start(StorageService& storage) {
  g_storage = &storage;
  if (storage.sdAvailable()) {
    storage.setSdHostOwned(true);
    msc_.vendorID("RECONCLV");
    msc_.productID("SD LOOT");
    msc_.productRevision("0.2");
    msc_.onRead(onMscRead);
    msc_.onWrite(onMscWrite);
    msc_.onStartStop(onMscStartStop);
    const bool started = msc_.begin(storage.sdBlockCount(), storage.sdBlockSize());
    msc_.mediaPresent(started);
    storage.appendAudit(started ? "msc-ready" : "msc-failed", "sd");
    Serial.printf("usb: MSC %s (%lu blocks)\n", started ? "ready" : "failed",
                  static_cast<unsigned long>(storage.sdBlockCount()));
  }
  keyboard_.begin();
  Serial.println("usb: HID keyboard + CDC up");
}

}  // namespace reconclave
