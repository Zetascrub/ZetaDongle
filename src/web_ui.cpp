#include "web_ui.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

#include "web_ui_page.h"

namespace reconclave {
namespace {

// Whole-request-body cap: the largest legitimate payload is a save (name +
// script text), so this is ScriptStore::kMaxScriptBytes plus slack for the
// JSON wrapper/name field - matching devices/cardputer-adv's main.cpp's own
// kMaxPayloadBytes-before-deserializeJson pattern rather than trusting
// ArduinoJson to bound an arbitrarily large input on its own.
constexpr size_t kMaxPayloadBytes = ScriptStore::kMaxScriptBytes + 256;

WebServer g_server(80);
ScriptStore* g_store = nullptr;

void sendJsonError(int code, const String& message) {
  JsonDocument doc;
  doc["error"] = message;
  String out;
  serializeJson(doc, out);
  g_server.send(code, "application/json", out);
}

void sendOk() {
  g_server.send(200, "application/json", "{\"ok\":true}");
}

bool payloadTooLarge() {
  if (g_server.arg("plain").length() <= kMaxPayloadBytes) return false;
  sendJsonError(413, "payload too large");
  return true;
}

void handleRoot() {
  g_server.send_P(200, "text/html; charset=utf-8", kWebUiPage);
}

void handleStatus() {
  JsonDocument doc;
  doc["ap_ssid"] = WiFi.softAPSSID();
  doc["ap_ip"] = WiFi.softAPIP().toString();
  doc["assigned"] = g_store->assignedName();
  doc["fire_count"] = g_store->fireCount();
  const unsigned long last_fired = g_store->lastFiredMs();
  if (last_fired == 0) {
    doc["last_fired_ms_ago"] = nullptr;
  } else {
    doc["last_fired_ms_ago"] = millis() - last_fired;
  }
  doc["uptime_ms"] = millis();
  String out;
  serializeJson(doc, out);
  g_server.send(200, "application/json", out);
}

void handleListScripts() {
  JsonDocument doc;
  doc["assigned"] = g_store->assignedName();
  JsonArray scripts = doc["scripts"].to<JsonArray>();
  for (const String& name : g_store->listNames()) {
    JsonObject entry = scripts.add<JsonObject>();
    entry["name"] = name;
  }
  String out;
  serializeJson(doc, out);
  g_server.send(200, "application/json", out);
}

void handleGetScript() {
  if (!g_server.hasArg("name")) { sendJsonError(400, "missing name"); return; }
  const String name = g_server.arg("name");
  String body;
  if (!g_store->load(name, body)) { sendJsonError(404, "not found"); return; }
  JsonDocument doc;
  doc["name"] = name;
  doc["body"] = body;
  String out;
  serializeJson(doc, out);
  g_server.send(200, "application/json", out);
}

void handleSaveScript() {
  if (payloadTooLarge()) return;
  JsonDocument doc;
  if (deserializeJson(doc, g_server.arg("plain"))) {
    sendJsonError(400, "invalid JSON");
    return;
  }
  const String name = doc["name"] | "";
  const String body = doc["body"] | "";
  String error;
  if (!g_store->save(name, body, error)) {
    sendJsonError(400, error);
    return;
  }
  sendOk();
}

void handleDeleteScript() {
  if (payloadTooLarge()) return;
  JsonDocument doc;
  if (deserializeJson(doc, g_server.arg("plain"))) {
    sendJsonError(400, "invalid JSON");
    return;
  }
  const String name = doc["name"] | "";
  if (!g_store->remove(name)) {
    sendJsonError(400, "delete failed (bad name or not found)");
    return;
  }
  sendOk();
}

void handleAssign() {
  if (payloadTooLarge()) return;
  JsonDocument doc;
  if (deserializeJson(doc, g_server.arg("plain"))) {
    sendJsonError(400, "invalid JSON");
    return;
  }
  const String name = doc["name"] | "";
  if (!g_store->setAssigned(name)) {
    sendJsonError(400, "unknown script name");
    return;
  }
  sendOk();
}

void handleNotFound() {
  sendJsonError(404, "not found");
}

}  // namespace

void WebUi::begin(ScriptStore& store, const String& ap_ssid) {
  g_store = &store;
  ap_ssid_ = ap_ssid;  // AP is already up (RadioManager owns it).

  Serial.printf("web_ui: serving on AP \"%s\" at http://%s/\n", ap_ssid_.c_str(),
                WiFi.softAPIP().toString().c_str());

  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/api/status", HTTP_GET, handleStatus);
  g_server.on("/api/scripts", HTTP_GET, handleListScripts);
  g_server.on("/api/script", HTTP_GET, handleGetScript);
  g_server.on("/api/script", HTTP_POST, handleSaveScript);
  g_server.on("/api/script/delete", HTTP_POST, handleDeleteScript);
  g_server.on("/api/assign", HTTP_POST, handleAssign);
  g_server.onNotFound(handleNotFound);
  g_server.begin();
}

void WebUi::handleClient() { g_server.handleClient(); }

IPAddress WebUi::apIp() const { return WiFi.softAPIP(); }

}  // namespace reconclave
