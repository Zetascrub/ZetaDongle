// Capability: the offline Zeta-themed web script editor. It can author, save,
// and *assign* scripts to the button — it deliberately has no "run" endpoint,
// so it never triggers an action. (It does not call TriggerPolicy because it
// performs no host-affecting action; arming a script is not firing it.)
#pragma once

#include "framework.h"
#include "web_ui.h"

namespace reconclave {

class WebConsoleModule : public Module {
 public:
  const char* capabilityId() const override { return "ui.web.console"; }
  void begin(Services& services) override;
  void loop(Services& services) override;

 private:
  WebUi web_;
};

}  // namespace reconclave
