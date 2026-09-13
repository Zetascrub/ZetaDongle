// Capability: run the button-assigned DuckyScript on the USB HID keyboard.
// This is the canonical "action" module — it does nothing until a completed
// physical-button press, and even then only after TriggerPolicy authorises it.
// There is no other path (network, timer, autonomous) to a keystroke.
#pragma once

#include "framework.h"

namespace reconclave {

class HidInjectorModule : public Module {
 public:
  const char* capabilityId() const override { return "hid.keyboard.inject"; }
  void loop(Services& services) override;
};

}  // namespace reconclave
