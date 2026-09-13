// Capability: a bidirectional raw-HID data channel to a cooperating host app.
// Adds a vendor-defined HID report (64-byte IN + 64-byte OUT, report id 4)
// alongside the keyboard, registered through UsbManager. This is the module
// that exercises the framework's composite-USB assembly path.
//
// Receiving bytes from the host is inert (no host-affecting action), so it does
// not pass through TriggerPolicy. If a future revision acted on a received
// command (e.g. triggered injection), that action would have to go through
// TriggerPolicy like any other — receiving data must never itself become a
// bypass of the button-only rule.
//
// NOTE: this compiles against the ESP32-S3 USB HID stack, but coexistence of a
// second HID report with the keyboard (report-ID arbitration, host
// enumeration) has NOT been verified on hardware — that is the specific bench
// check for this module.
#pragma once

#include <USBHID.h>

#include "framework.h"

namespace reconclave {

class RawHidChannelModule : public Module, public USBHIDDevice {
 public:
  const char* capabilityId() const override { return "usb.rawhid.channel"; }
  void begin(Services& services) override;
  void loop(Services& services) override;

  // USBHIDDevice
  uint16_t _onGetDescriptor(uint8_t* buffer) override;
  void _onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len) override;

  static constexpr uint8_t kReportId = 4;
  static constexpr uint16_t kReportBytes = 64;

 private:
  UsbManager* usb_ = nullptr;
  volatile bool echo_pending_ = false;
  uint8_t rx_[kReportBytes] = {0};
};

}  // namespace reconclave
