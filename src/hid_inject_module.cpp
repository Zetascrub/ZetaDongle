#include "hid_inject_module.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "ducky_script.h"
#include "node_service.h"
#include "usb_manager.h"

namespace reconclave {

void HidInjectModule::begin(Services& s) {
  // App::services_ outlives every module, so capturing the addresses of the
  // long-lived service objects is safe.
  TriggerPolicy* trigger = &s.trigger;
  UsbManager* usb = &s.usb;
  Trust* trust = &s.node.trust();
  Config* config = &s.config;
  NodeService* node = &s.node;

  s.node.registerCapability(
      "input.hid.keystroke", "trusted",
      [trigger, usb, trust, config, node](JsonVariantConst args, const RequestContext& ctx) -> CapabilityOutcome {
        CapabilityOutcome out;

        TriggerContext tctx;
        tctx.source = TriggerSource::Network;
        tctx.authenticated = ctx.authenticated;
        String reason;
        tctx.scope_verified = trust->verifyHostedScope(ctx.peer, args, "input.hid.keystroke",
                                                        node->deviceId(), config->getString("host_inst", ""), reason);
        const String scope_reason = reason;
        if (!trigger->authorize("input.hid.keystroke", tctx, reason)) {
          if (!tctx.scope_verified && scope_reason.length()) reason = scope_reason;
          out.status = "rejected";
          out.error_code = "SCOPE_REQUIRED";
          out.error_message = reason;
          Serial.printf("hid.inject: fail-closed (%s)\n", reason.c_str());
          return out;
        }

        const String script = args["script"] | "";
        if (!script.length() || script.length() > 500) {
          out.status = "rejected";
          out.error_code = "INVALID_SCRIPT";
          out.error_message = "script must contain 1-500 characters";
          return out;
        }
        const DuckyScriptResult r = runDuckyScript(script, usb->keyboard());
        JsonDocument res;
        res["steps_run"] = r.steps_run;
        res["line_number"] = r.line_number;
        String result_json;
        serializeJson(res, result_json);
        out.result_json = result_json;
        if (r.ok) {
          out.status = "ok";
        } else {
          out.status = "error";
          out.error_code = "SCRIPT_ERROR";
          out.error_message = r.error;
        }
        return out;
      });
}

}  // namespace reconclave
