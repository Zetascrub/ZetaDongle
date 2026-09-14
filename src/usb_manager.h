// Owns the USB HID keyboard used by the HID capability. USB.begin() is already
// called by the core before setup() (ARDUINO_USB_CDC_ON_BOOT=1); start() adds
// the keyboard on top of the running CDC device.
#pragma once

#include <USBHIDKeyboard.h>
#include <USBMSC.h>

namespace reconclave {

class StorageService;

class UsbManager {
 public:
  void start(StorageService& storage);
  USBHIDKeyboard& keyboard() { return keyboard_; }

 private:
  USBHIDKeyboard keyboard_;
  USBMSC msc_;
};

}  // namespace reconclave
