// Owns the Wi-Fi/BLE radios. Today it brings up the standalone SoftAP the web
// console runs on (moved here from web_ui so the radio has a single owner). BLE
// and STA/scan are declared as future extension points, not yet implemented.
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace reconclave {

class RadioManager {
 public:
  // Brings up a standalone AP named "Zeta-Dongle-<last 4 of AP MAC>". The
  // passphrase is the whole access-control boundary for the offline console.
  bool beginAp(const char* password);

  const String& apSsid() const { return ap_ssid_; }
  IPAddress apIp() const;
  bool apUp() const { return ap_up_; }

  // Future extension points (not implemented): STA join, scan, promiscuous
  // sniffing, BLE advertise/scan/HID. Declared to mark the intended surface.

 private:
  String ap_ssid_;
  bool ap_up_ = false;
};

}  // namespace reconclave
