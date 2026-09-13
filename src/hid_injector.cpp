#include "hid_injector.h"

#include <Arduino.h>

#include "ducky_script.h"
#include "storage_service.h"
#include "ui_service.h"
#include "usb_manager.h"

namespace reconclave {

void HidInjectorModule::loop(Services& s) {
  // Only fires on a completed physical-button press...
  if (!s.input.consumePrimaryPress()) return;

  // ...and only if policy authorises it. This is the single gate; there is no
  // code path that types a key without passing this check.
  String reason;
  if (!s.trigger.authorize(capabilityId(), TriggerSource::PhysicalButton, reason)) {
    Serial.printf("hid.inject: denied (%s)\n", reason.c_str());
    return;
  }

  ScriptStore& store = s.storage.scripts();
  const String assigned = store.assignedName();
  if (assigned.length() == 0) {
    Serial.println("hid.inject: button pressed, nothing assigned");
    s.ui.flash(CRGB::Orange, 200);
    return;
  }

  String body;
  if (!store.load(assigned, body)) {
    Serial.printf("hid.inject: assigned script \"%s\" failed to load\n", assigned.c_str());
    s.ui.flash(CRGB::Red, 200);
    return;
  }

  Serial.printf("hid.inject: running \"%s\"\n", assigned.c_str());
  const DuckyScriptResult result = runDuckyScript(body, s.usb.keyboard());
  if (result.ok) {
    Serial.printf("  ok, %d step(s)\n", result.steps_run);
  } else {
    Serial.printf("  stopped at line %d: %s (%d step(s) ran)\n", result.line_number,
                  result.error.c_str(), result.steps_run);
  }
  store.recordFire();
  s.ui.flashFired(store.fireCount());
}

}  // namespace reconclave
