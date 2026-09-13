#include "raw_hid_channel.h"

#include <Arduino.h>
#include <string.h>

#include "usb_manager.h"

namespace reconclave {
namespace {
// Vendor-defined raw HID: usage page 0xFF00, one 64-byte IN report and one
// 64-byte OUT report, tagged with report id 4 so it coexists with the
// keyboard's own report id(s) on the shared HID interface.
const uint8_t kReportDescriptor[] = {
    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        // Usage (0x01)
    0xA1, 0x01,        // Collection (Application)
    0x85, RawHidChannelModule::kReportId,  //   Report ID (4)
    0x09, 0x02,        //   Usage (0x02) - input
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, RawHidChannelModule::kReportBytes,  //   Report Count (64)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x09, 0x03,        //   Usage (0x03) - output
    0x91, 0x02,        //   Output (Data,Var,Abs)
    0xC0               // End Collection
};
}  // namespace

void RawHidChannelModule::begin(Services& s) {
  usb_ = &s.usb;
  usb_->addHidDevice(this, sizeof(kReportDescriptor));
}

uint16_t RawHidChannelModule::_onGetDescriptor(uint8_t* buffer) {
  memcpy(buffer, kReportDescriptor, sizeof(kReportDescriptor));
  return sizeof(kReportDescriptor);
}

void RawHidChannelModule::_onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len) {
  // Host -> device. Copy out of the USB buffer and defer the echo to loop();
  // this runs in USB task context, so keep it minimal.
  if (report_id != kReportId) return;
  const uint16_t n = len < kReportBytes ? len : kReportBytes;
  memcpy(rx_, buffer, n);
  if (n < kReportBytes) memset(rx_ + n, 0, kReportBytes - n);
  echo_pending_ = true;
}

void RawHidChannelModule::loop(Services& s) {
  (void)s;
  if (!echo_pending_ || usb_ == nullptr) return;
  echo_pending_ = false;
  // Round-trip the received frame back to the host as proof of a working
  // bidirectional channel. A real consumer would parse rx_ into a protocol.
  usb_->sendHidReport(kReportId, rx_, kReportBytes);
  Serial.println("rawhid: echoed a 64-byte frame back to host");
}

}  // namespace reconclave
