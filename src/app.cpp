#include "app.h"

#include <Arduino.h>

namespace reconclave {
namespace {
// Fixed, documented default (README.md): a standalone/offline field tool, so
// the AP passphrase itself is the access-control boundary. Change and reflash
// where a fixed default isn't acceptable.
constexpr const char* kApPassword = "TinkerHackFlash";
constexpr unsigned long kHeartbeatIntervalMs = 5000;
}  // namespace

App::App()
    : services_{usb_, radio_, storage_, ui_, input_, config_, trigger_} {}

void App::addModule(Module& module) { modules_.push_back(&module); }

void App::begin() {
  Serial.begin(115200);
  delay(200);  // let USB CDC enumerate before printing
  Serial.println();
  Serial.println("zeta-dongle: framework boot");

  config_.begin();
  storage_.begin();
  radio_.beginAp(kApPassword);
  input_.begin(pins::kButton);
  ui_.begin();  // blocking boot logo/animation

  // Modules register their interfaces (USB/HID devices, HTTP routes, ...).
  for (Module* m : modules_) {
    Serial.printf("module: begin %s\n", m->capabilityId());
    m->begin(services_);
  }

  // USB comes up last, after every module has registered its HID devices, so
  // the composite descriptor is complete.
  usb_.start();
  Serial.println("zeta-dongle: ready");
}

void App::loop() {
  const unsigned long now = millis();
  input_.poll();

  for (Module* m : modules_) m->loop(services_);

  ui_.tick(now, input_.pressedNow());
  ui_.updateDisplay(storage_.scripts(), radio_.apSsid(), radio_.apIp());

  static unsigned long next_heartbeat = 0;
  if (now >= next_heartbeat) {
    Serial.printf("heartbeat: alive, fires=%u, assigned=\"%s\", ap=%s\n",
                  static_cast<unsigned>(storage_.scripts().fireCount()),
                  storage_.scripts().assignedName().c_str(), radio_.apSsid().c_str());
    next_heartbeat = now + kHeartbeatIntervalMs;
  }
}

}  // namespace reconclave
