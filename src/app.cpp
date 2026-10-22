#include "app.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <tusb.h>

#include "ducky_script.h"
#include "instance_link.h"

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

void App::runArmedPayload(TriggerSource source) {
  const String name = storage_.armedPayload();
  if (!name.length()) {
    led_.flash(CRGB(255, 170, 28), 300);
    display_.showRunResult("none", false, "NOT ARMED");
    return;
  }
  TriggerContext ctx;
  ctx.source = source;
  String reason;
  if (!trigger_.authorize("input.hid.keystroke", ctx, reason)) return;
  String script;
  if (!storage_.loadPayload(name, script)) {
    storage_.recordRun(false, name);
    led_.flash(CRGB(255, 79, 135), 400);
    display_.showRunResult(name, false, "LOAD ERROR");
    return;
  }
  Serial.printf("standalone: running payload \"%s\"\n", name.c_str());
  const DuckyScriptResult result = runDuckyScript(script, usb_.keyboard());
  storage_.recordRun(result.ok, name);
  led_.flash(result.ok ? CRGB(0, 205, 215) : CRGB(255, 79, 135), 500);
  display_.showRunResult(name, result.ok, result.ok ? String(result.steps_run) + " STEPS" : result.error);
}

void App::pollHostedCommand() {
  const String instance = config_.getString("host_inst", "");
  const String device = config_.getString("host_dev", "");
  const String key = config_.getString("host_key", "");
  JsonDocument response;
  if (!reconclave_hosted::pullCommand(instance, device, key, response)) {
    Serial.println("instance: command poll failed");
    return;
  }
  JsonArrayConst commands = response["commands"].as<JsonArrayConst>();
  if (commands.isNull() || commands.size() != 1) return;
  JsonObjectConst command = commands[0].as<JsonObjectConst>();
  const String id = command["id"] | "";
  if (!reconclave_hosted::validId(id, 16)) return;
  const String script = command["arguments"]["script"] | "";
  JsonDocument result;
  String status = "rejected";
  if (command["device_id"] != device || command["hardware_id"] != node_.deviceId() ||
      command["capability"] != "input.hid.keystroke" || !script.length() || script.length() > 500) {
    result["error"] = "invalid or misdirected HID command";
  } else if (!tud_hid_ready()) {
    status = "failed";
    result["error"] = "USB HID host is not ready";
  } else {
    const DuckyScriptResult run = runDuckyScript(script, usb_.keyboard());
    storage_.recordRun(run.ok, "remote");
    status = run.ok ? "done" : "failed";
    result["steps_run"] = run.steps_run;
    result["line_number"] = run.line_number;
    if (!run.ok) result["error"] = run.error;
    display_.showRunResult("remote", run.ok, run.ok ? String(run.steps_run) + " STEPS" : run.error);
  }
  if (!reconclave_hosted::reportCommand(instance, device, key, id, status, result))
    Serial.printf("instance: command %s result not recorded\n", id.c_str());
}

void App::pollSerialCommands() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c != '\n') {
      if (serial_line_.length() < 512) serial_line_ += c;
      continue;
    }
    String line = serial_line_;
    serial_line_ = "";
    line.trim();
    if (line.startsWith("instance-json ")) {
      JsonDocument request;
      const DeserializationError error = deserializeJson(request, line.substring(14));
      const String instance_id = request["instance_id"] | "";
      const String device_id = request["device_id"] | "";
      const String credential = request["credential"] | "";
      if (error || !reconclave_hosted::validId(instance_id, 24) ||
          !reconclave_hosted::validId(device_id, 16) ||
          !reconclave_hosted::validId(credential, 43)) {
        Serial.println("instance: invalid enrollment request");
        continue;
      }
      config_.setString("host_inst", instance_id);
      config_.setString("host_dev", device_id);
      config_.setString("host_key", credential);
      next_hosted_check_ms_ = millis();
      hosted_connected_ = false;
      Serial.println("instance: saved; awaiting Wi-Fi and verified HTTPS check-in");
    } else if (line.startsWith("wifi-json ")) {
      JsonDocument request;
      const DeserializationError error = deserializeJson(request, line.substring(10));
      const String ssid = request["ssid"] | "";
      const String pass = request["password"] | "";
      if (error || ssid.isEmpty() || ssid.length() > 32 || pass.length() > 63) {
        Serial.println("wifi: invalid provisioning request");
        continue;
      }
      config_.setString("wifi_ssid", ssid);
      config_.setString("wifi_pass", pass);
      Serial.println("wifi: saved to device settings; rebooting");
      Serial.flush();
      delay(300);
      ESP.restart();
    } else if (line.startsWith("wifi ")) {
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
      Serial.printf("status: autorun=%s armed=%s\n", config_.getUInt("auto_run") ? "on" : "off",
                    storage_.armedPayload().c_str());
    } else if (line == "autorun on" || line == "autorun off") {
      config_.setUInt("auto_run", line == "autorun on" ? 1 : 0);
      Serial.printf("autorun: %s\n", line == "autorun on" ? "on" : "off");
    } else if (line.length() > 0) {
      Serial.println("commands: wifi-json | instance-json | wifi <ssid> <pass> | autorun on/off | status");
    }
  }
}

void App::loop() {
  const unsigned long now = millis();
  pollSerialCommands();
  radio_.poll();
  input_.poll();
  node_.handleClient();
  for (Module* m : modules_) m->loop(services_);
  if (input_.consumeLongPress()) {
    storage_.armPayload("");
    config_.setUInt("auto_run", 0);
    storage_.appendAudit("disarm", "long-button");
    led_.flash(CRGB(0, 205, 215), 350);
    display_.showRunResult("safe", true, "DISARMED");
  }
  if (input_.consumePrimaryPress()) runArmedPayload();
  // shortcut: automatic injection fires once per boot; add reattach handling if battery-backed use is needed.
  if (auto_run_pending_ && millis() >= 3000 && tud_hid_ready()) {
    auto_run_pending_ = false;
    if (config_.getUInt("auto_run") && storage_.armedPayload().length())
      runArmedPayload(TriggerSource::UsbAttach);
  }

  led_.setLinked(radio_.staConnected());
  led_.tick(now);
  display_.update(storage_, radio_, radio_.staConnected());

  static unsigned long next_heartbeat = 0;
  if (now >= next_heartbeat) {
    Serial.printf("heartbeat: id=%s link=%s ip=%s\n", node_.deviceId().c_str(),
                  radio_.staConnected() ? "up" : "down", radio_.staIp().toString().c_str());
    next_heartbeat = now + kHeartbeatIntervalMs;
  }
  if (radio_.staConnected() && config_.getString("host_inst", "").length() &&
      static_cast<int32_t>(millis() - next_hosted_check_ms_) >= 0) {
    JsonDocument details;
    JsonObject status = details["status"].to<JsonObject>();
    status["armed"] = storage_.armedPayload();
    status["run_count"] = storage_.runCount();
    status["failure_count"] = storage_.failureCount();
    status["storage_used"] = storage_.usedBytes();
    status["storage_total"] = storage_.totalBytes();
    status["sd_available"] = storage_.sdAvailable();
    status["uptime_ms"] = millis();
    status["free_heap"] = ESP.getFreeHeap();
    storage_.recentRunEvents(details["runs"].to<JsonArray>());
    const bool connected = reconclave_hosted::checkIn(
        config_.getString("host_inst", ""), config_.getString("host_dev", ""),
        config_.getString("host_key", ""), node_.deviceId(), "0.2.1-hosted.3-direct", &details);
    next_hosted_check_ms_ = millis() + (connected ? 300000UL : 10000UL);
    hosted_connected_ = connected;
    Serial.println(connected ? "instance: connected" : "instance: check-in pending");
  }
  if (radio_.staConnected() && hosted_connected_ && tud_hid_ready() &&
      static_cast<int32_t>(millis() - next_hosted_command_ms_) >= 0) {
    next_hosted_command_ms_ = millis() + 3000UL;
    pollHostedCommand();
  }
}

}  // namespace reconclave
