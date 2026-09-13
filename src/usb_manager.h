// Owns the device's USB personality. The keyboard is always present; capability
// modules that need a raw/custom HID interface register their USBHIDDevice here
// via addHidDevice() during their begin(), and the App calls start() once, after
// every module has registered, so the composite HID descriptor is assembled with
// all devices included.
//
// Note: USB.begin() is already called by the core before setup() runs (build flag
// ARDUINO_USB_CDC_ON_BOOT=1), so start() adds HID on top of the already-running
// CDC device.
#pragma once

#include <USBHID.h>
#include <USBHIDKeyboard.h>

namespace reconclave {

class UsbManager {
 public:
  // Register a custom HID device (e.g. a raw in/out channel). Call from a
  // module's begin(); it takes effect at start(). descriptor_len is the byte
  // length of the report descriptor the device returns from _onGetDescriptor.
  void addHidDevice(USBHIDDevice* device, uint16_t descriptor_len);

  // Send a report on the shared custom-HID interface (report id must match the
  // registered device's descriptor).
  bool sendHidReport(uint8_t report_id, const void* data, size_t len);

  // Begins the keyboard and, if any custom HID devices were registered, the
  // shared custom-HID interface. Called once by the App after module begin().
  void start();

  USBHIDKeyboard& keyboard() { return keyboard_; }

 private:
  USBHIDKeyboard keyboard_;
  USBHID hid_;              // shared interface for custom (non-keyboard) HID devices
  bool has_custom_ = false;
};

}  // namespace reconclave
