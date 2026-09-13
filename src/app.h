// The App core: owns the services, holds the registered modules, and runs the
// begin/loop lifecycle. main.cpp just constructs it, registers modules, and
// forwards setup()/loop().
//
// begin() order matters: services come up, then every module's begin() runs
// (so a module can register a USB/HID interface or HTTP routes), then USB is
// started last so the composite descriptor includes everything modules added.
#pragma once

#include <vector>

#include "framework.h"
#include "radio_manager.h"
#include "storage_service.h"
#include "ui_service.h"
#include "usb_manager.h"

namespace reconclave {

class App {
 public:
  App();

  // Register a module. It must outlive the App (typically a static/global).
  void addModule(Module& module);

  void begin();
  void loop();

 private:
  UsbManager usb_;
  RadioManager radio_;
  StorageService storage_;
  UiService ui_;
  InputService input_;
  Config config_;
  TriggerPolicy trigger_;
  Services services_;
  std::vector<Module*> modules_;
};

}  // namespace reconclave
