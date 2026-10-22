// Capability: input.hid.keystroke — run a DuckyScript payload on the USB HID
// keyboard, invoked by the coordinator over the authenticated protocol.
//
// It is registered as a "trusted" capability, so NodeService authenticates the
// request (HMAC) before the handler runs. The handler then requires a verified
// signed hosted scope via TriggerPolicy. The scope is bound to this instance,
// the attached computer and the exact DuckyScript before anything is typed.
#pragma once

#include "framework.h"

namespace reconclave {

class HidInjectModule : public Module {
 public:
  const char* capabilityId() const override { return "input.hid.keystroke"; }
  void begin(Services& services) override;
};

}  // namespace reconclave
