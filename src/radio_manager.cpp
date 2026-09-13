#include "radio_manager.h"

#include <WiFi.h>

namespace reconclave {

bool RadioManager::beginAp(const char* password) {
  WiFi.mode(WIFI_AP);
  String mac = WiFi.softAPmacAddress();
  mac.replace(":", "");
  const String suffix = mac.substring(mac.length() - 4);
  ap_ssid_ = "Zeta-Dongle-" + suffix;
  ap_up_ = WiFi.softAP(ap_ssid_.c_str(), password);
  Serial.printf("radio: AP \"%s\" %s, browse to http://%s/\n", ap_ssid_.c_str(),
                ap_up_ ? "up" : "FAILED", WiFi.softAPIP().toString().c_str());
  return ap_up_;
}

IPAddress RadioManager::apIp() const { return WiFi.softAPIP(); }

}  // namespace reconclave
