#include "usb_manager.h"

#include <Arduino.h>

namespace reconclave {

void UsbManager::addHidDevice(USBHIDDevice* device, uint16_t descriptor_len) {
  hid_.addDevice(device, descriptor_len);
  has_custom_ = true;
}

bool UsbManager::sendHidReport(uint8_t report_id, const void* data, size_t len) {
  return hid_.SendReport(report_id, data, len);
}

void UsbManager::start() {
  keyboard_.begin();
  if (has_custom_) hid_.begin();
  Serial.printf("usb: HID up (keyboard%s)\n", has_custom_ ? " + custom" : "");
}

}  // namespace reconclave
