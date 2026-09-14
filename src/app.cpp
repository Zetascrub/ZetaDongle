#include "app.h"

#include <Arduino.h>

#include "ducky_script.h"

namespace reconclave {
namespace {
constexpr unsigned long kHeartbeatIntervalMs = 5000;
}  // namespace

App::App() : services_{usb_, radio_, node_, led_, input_, config_, trigger_, storage_, display_} {}

void App::addModule(Module& module) { modules_.push_back(&module); }

void App::begin() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("reconclave t-dongle node: framework boot");

  config_.begin();
  storage_.begin();
  led_.begin();
  input_.begin(pins::kButton);
  display_.begin();
  usb_.start(storage_);

  radio_.init();  // WiFi netif up first — MAC read and server bind depend on it
  node_.beginIdentity(config_);

  const String ssid = config_.getString("wifi_ssid", "");
  const String pass = config_.getString("wifi_pass", "");
  radio_.connect(ssid, pass, node_.deviceId(), 80);

  node_.startServer(led_, config_, radio_, storage_);

  for (Module* m : modules_) {
    Serial.printf("module: begin %s\n", m->capabilityId());
    m->begin(services_);
  }
  Serial.println("reconclave t-dongle node: ready");
}

void App::runArmedPayload() {
  const String name = storage_.armedPayload();
  if (!name.length()) {
    led_.flash(CRGB(255, 170, 28), 300);
    display_.showRunResult("none", false, "NOT ARMED");
    return;
  }
  TriggerContext ctx;
  ctx.source = TriggerSource::PhysicalButton;
  String reason;
  if (!trigger_.authorize("input.hid.keystroke", ctx, reason)) return;
  String script;
  if (!storage_.loadPayload(name, script)) {
    storage_.recordRun(false);
    led_.flash(CRGB(255, 79, 135), 400);
    display_.showRunResult(name, false, "LOAD ERROR");
    return;
  }
  Serial.printf("standalone: running payload \"%s\"\n", name.c_str());
  const DuckyScriptResult result = runDuckyScript(script, usb_.keyboard());
  storage_.recordRun(result.ok);
  storage_.appendAudit(result.ok ? "payload-ok" : "payload-failed", name);
  led_.flash(result.ok ? CRGB(0, 205, 215) : CRGB(255, 79, 135), 500);
  display_.showRunResult(name, result.ok, result.ok ? String(result.steps_run) + " STEPS" : result.error);
}

void App::pollSerialCommands() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c != '\n') {
      if (serial_line_.length() < 160) serial_line_ += c;
      continue;
    }
    String line = serial_line_;
    serial_line_ = "";
    line.trim();
    if (line.startsWith("wifi ")) {
      // "wifi <ssid> <pass>" — split on the first space after the command; the
      // password may contain spaces, the SSID may not.
      const String rest = line.substring(5);
      const int sep = rest.indexOf(' ');
      if (sep <= 0) {
        Serial.println("usage: wifi <ssid> <pass>");
        continue;
      }
      const String ssid = rest.substring(0, sep);
      const String pass = rest.substring(sep + 1);
      config_.setString("wifi_ssid", ssid);
      config_.setString("wifi_pass", pass);
      Serial.printf("wifi: stored SSID \"%s\" (%d-char pass) - rebooting to join\n",
                    ssid.c_str(), pass.length());
      delay(200);
      ESP.restart();
    } else if (line == "status") {
      Serial.printf("status: id=%s link=%s ip=%s\n", node_.deviceId().c_str(),
                    radio_.staConnected() ? "up" : "down", radio_.staIp().toString().c_str());
    } else if (line.length() > 0) {
      Serial.println("commands: wifi <ssid> <pass> | status");
    }
  }
}

void App::loop() {
  const unsigned long now = millis();
  pollSerialCommands();
  input_.poll();
  node_.handleClient();
  for (Module* m : modules_) m->loop(services_);
  if (input_.consumeLongPress()) {
    storage_.armPayload("");
    storage_.appendAudit("disarm", "long-button");
    led_.flash(CRGB(0, 205, 215), 350);
    display_.showRunResult("safe", true, "DISARMED");
  }
  if (input_.consumePrimaryPress()) runArmedPayload();

  led_.setLinked(radio_.staConnected());
  led_.tick(now);
  display_.update(storage_, radio_, radio_.staConnected());

  static unsigned long next_heartbeat = 0;
  if (now >= next_heartbeat) {
    Serial.printf("heartbeat: id=%s link=%s ip=%s\n", node_.deviceId().c_str(),
                  radio_.staConnected() ? "up" : "down", radio_.staIp().toString().c_str());
    next_heartbeat = now + kHeartbeatIntervalMs;
  }
}

}  // namespace reconclave
