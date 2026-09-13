#include "script_store.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

namespace reconclave {
namespace {
constexpr const char* kMetaPath = "/meta.json";
constexpr const char* kScriptsDir = "/scripts";
}  // namespace

bool ScriptStore::validName(const String& name) {
  if (name.length() == 0 || name.length() > 32) return false;
  for (size_t i = 0; i < name.length(); i++) {
    const char c = name[i];
    const bool ok = isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

String ScriptStore::scriptPath(const String& name) const {
  return String(kScriptsDir) + "/" + name + ".txt";
}

bool ScriptStore::begin() {
  if (!LittleFS.begin(/*formatOnFail=*/true)) {
    Serial.println("script_store: LittleFS mount (and format) failed");
    return false;
  }
  if (!LittleFS.exists(kScriptsDir)) {
    LittleFS.mkdir(kScriptsDir);
  }
  loadMeta();
  began_ = true;
  Serial.printf("script_store: mounted, %u/%u bytes used, assigned=\"%s\"\n",
                static_cast<unsigned>(LittleFS.usedBytes()),
                static_cast<unsigned>(LittleFS.totalBytes()), assigned_.c_str());
  return true;
}

void ScriptStore::loadMeta() {
  File f = LittleFS.open(kMetaPath, "r");
  if (!f) return;  // First boot: no meta yet, defaults stand.
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("script_store: meta.json parse failed: %s\n", err.c_str());
    return;
  }
  assigned_ = doc["assigned"] | "";
  fire_count_ = doc["fire_count"] | 0;
  // A previously-assigned script may have been deleted by hand (or the
  // metadata could simply be stale) - don't let a dangling name survive.
  if (assigned_.length() > 0 && !LittleFS.exists(scriptPath(assigned_))) {
    assigned_ = "";
  }
}

void ScriptStore::saveMeta() const {
  JsonDocument doc;
  doc["assigned"] = assigned_;
  doc["fire_count"] = fire_count_;
  File f = LittleFS.open(kMetaPath, "w");
  if (!f) {
    Serial.println("script_store: failed to open meta.json for write");
    return;
  }
  serializeJson(doc, f);
  f.close();
}

std::vector<String> ScriptStore::listNames() const {
  std::vector<String> names;
  File dir = LittleFS.open(kScriptsDir);
  if (!dir || !dir.isDirectory()) return names;
  File entry = dir.openNextFile();
  while (entry) {
    String name = entry.name();  // Just the filename, not the full path.
    if (name.endsWith(".txt")) {
      names.push_back(name.substring(0, name.length() - 4));
    }
    entry = dir.openNextFile();
  }
  return names;
}

bool ScriptStore::load(const String& name, String& body_out) const {
  if (!validName(name)) return false;
  File f = LittleFS.open(scriptPath(name), "r");
  if (!f) return false;
  body_out = f.readString();
  f.close();
  return true;
}

bool ScriptStore::save(const String& name, const String& body, String& error_out) {
  if (!validName(name)) {
    error_out = "name must be 1-32 chars of letters/digits/_/-";
    return false;
  }
  if (body.length() > kMaxScriptBytes) {
    error_out = "script exceeds " + String(kMaxScriptBytes) + " byte limit";
    return false;
  }
  File f = LittleFS.open(scriptPath(name), "w");
  if (!f) {
    error_out = "failed to open file for write (storage full?)";
    return false;
  }
  const size_t written = f.print(body);
  f.close();
  if (written != body.length()) {
    error_out = "short write - storage may be full";
    return false;
  }
  return true;
}

bool ScriptStore::remove(const String& name) {
  if (!validName(name)) return false;
  if (!LittleFS.remove(scriptPath(name))) return false;
  if (assigned_ == name) {
    assigned_ = "";
    saveMeta();
  }
  return true;
}

bool ScriptStore::setAssigned(const String& name) {
  if (name.length() > 0) {
    if (!validName(name) || !LittleFS.exists(scriptPath(name))) return false;
  }
  assigned_ = name;
  saveMeta();
  return true;
}

void ScriptStore::recordFire() {
  fire_count_++;
  last_fired_ms_ = millis();
  saveMeta();
}

}  // namespace reconclave
