// Zeta-Dongle firmware framework — core contracts.
//
// The device is a set of self-registering capability *modules* serviced by a
// small App core over a handful of shared *services*. Adding a feature means
// adding a Module, not editing a monolith.
//
// A hard invariant of this framework: every host-affecting action passes
// through TriggerPolicy. Today only a physical-button trigger is authorised;
// there is no autonomous or network path to an action. A future signed-scope /
// evidence-logging check slots into TriggerPolicy without touching any module.
#pragma once

#include <Arduino.h>

namespace reconclave {

// Board pins (see README.md — confirmed against the real unit).
namespace pins {
constexpr int kButton = 0;       // GPIO0 boot-strap button.
constexpr int kBacklight = 38;   // ST7735 backlight, active-LOW.
constexpr int kLedData = 40;     // APA102 data.
constexpr int kLedClock = 39;    // APA102 clock.
}  // namespace pins

// Where an attempt to run an action came from. Only PhysicalButton is
// authorised today; the rest exist so callers name their source honestly and
// so future policy can distinguish them.
enum class TriggerSource { PhysicalButton, Network, Timer, Unknown };

// The single chokepoint every action capability must call before doing
// anything that affects the host. Keep this the *only* place authorisation is
// decided so the "button-only, no autonomous/remote path" guarantee can't be
// bypassed by an individual module.
class TriggerPolicy {
 public:
  bool authorize(const char* capability_id, TriggerSource source, String& reason) const {
    (void)capability_id;
    if (source == TriggerSource::PhysicalButton) return true;
    reason = "only a physical-button trigger is authorised (no autonomous/remote path)";
    return false;
  }
};

// Debounced-ish edge detection for the single physical button. INPUT_PULLUP:
// idle HIGH, pressed LOW. A "primary press" is a completed press-then-release,
// matching the original firmware's fire-on-release behaviour.
class InputService {
 public:
  void begin(int button_pin) {
    pin_ = button_pin;
    pinMode(pin_, INPUT_PULLUP);
  }
  void poll() {
    pressed_now_ = digitalRead(pin_) == LOW;
    if (pressed_now_ != last_pressed_) {
      if (!pressed_now_) released_edge_ = true;  // press -> release completes it
      last_pressed_ = pressed_now_;
    }
  }
  bool pressedNow() const { return pressed_now_; }
  // Returns true exactly once per completed press-release, then clears.
  bool consumePrimaryPress() {
    const bool e = released_edge_;
    released_edge_ = false;
    return e;
  }

 private:
  int pin_ = 0;
  bool last_pressed_ = false;
  bool pressed_now_ = false;
  bool released_edge_ = false;
};

// Thin persisted key/value for module settings (NVS). ScriptStore still owns
// its own persistence; this is for small module config that doesn't warrant a
// file. Header-declared, defined in framework.cpp.
class Config {
 public:
  void begin();
  uint32_t getUInt(const char* key, uint32_t fallback = 0);
  void setUInt(const char* key, uint32_t value);
  String getString(const char* key, const String& fallback = String());
  void setString(const char* key, const String& value);
};

// Services are constructed once by the App and injected into every module.
// Forward-declared here; modules include the concrete headers they use.
class UsbManager;
class RadioManager;
class StorageService;
class UiService;

struct Services {
  UsbManager& usb;
  RadioManager& radio;
  StorageService& storage;
  UiService& ui;
  InputService& input;
  Config& config;
  TriggerPolicy& trigger;
};

// A capability module. `capabilityId` is a stable dotted name (e.g.
// "hid.keyboard.inject") so modules, logs and future authz can refer to it.
class Module {
 public:
  virtual ~Module() = default;
  virtual const char* capabilityId() const = 0;
  virtual void begin(Services& services) { (void)services; }
  virtual void loop(Services& services) { (void)services; }
};

}  // namespace reconclave
