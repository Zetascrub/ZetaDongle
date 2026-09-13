// Zeta-Dongle firmware entry point.
//
// The device is built as a capability-module framework (see framework.h and
// app.h): the App core owns shared services (USB, radio, storage, UI, input,
// config) and runs a list of self-registering modules. Adding a feature is
// adding a module, not editing this file.
//
// Project invariant, unchanged from the original firmware: the physical button
// is the ONLY thing that can trigger an action. Every action capability routes
// through TriggerPolicy (framework.h), which authorises only a physical-button
// trigger — there is no network, timer, or autonomous path to a keystroke. The
// web console (ui.web.console) can author and *assign* scripts to the button
// but has no "run" endpoint.
//
// Registered capabilities:
//   hid.keyboard.inject   - run the button-assigned DuckyScript (action, gated)
//   ui.web.console        - offline Zeta web script editor (authoring only)
//   usb.rawhid.channel    - bidirectional raw-HID data channel (proof of the
//                           framework's composite-USB extension path)
#include "app.h"
#include "hid_injector.h"
#include "raw_hid_channel.h"
#include "web_console.h"

namespace {
reconclave::App g_app;
reconclave::HidInjectorModule g_hid_injector;
reconclave::WebConsoleModule g_web_console;
reconclave::RawHidChannelModule g_raw_hid;
}  // namespace

void setup() {
  g_app.addModule(g_hid_injector);
  g_app.addModule(g_web_console);
  g_app.addModule(g_raw_hid);
  g_app.begin();
}

void loop() { g_app.loop(); }
