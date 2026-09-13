// Standalone WiFi AP + HTTP server hosting the Zeta-themed script editor
// (see src/web_ui_page.h). Deliberately offers no way to fire a script from
// the network - only to write/save/assign one to the physical button; firing
// itself stays button-only exactly as before this feature existed. See
// devices/t-dongle-s3/README.md.
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

#include "script_store.h"

namespace reconclave {

class WebUi {
 public:
  // Starts the HTTP server on the already-running AP (brought up by
  // RadioManager). `store` must outlive the WebUi instance - handlers hold a
  // reference to it, not a copy. `ap_ssid` is used only for display/reporting.
  void begin(ScriptStore& store, const String& ap_ssid);

  // Must be called every loop() iteration (WebServer is not interrupt-driven).
  void handleClient();

  const String& apSsid() const { return ap_ssid_; }
  IPAddress apIp() const;

 private:
  String ap_ssid_;
};

}  // namespace reconclave
