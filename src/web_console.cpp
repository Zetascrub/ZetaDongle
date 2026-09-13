#include "web_console.h"

#include "radio_manager.h"
#include "storage_service.h"

namespace reconclave {

void WebConsoleModule::begin(Services& s) {
  // The AP is already up (RadioManager owns it); WebUi just serves on it.
  web_.begin(s.storage.scripts(), s.radio.apSsid());
}

void WebConsoleModule::loop(Services& s) {
  (void)s;
  web_.handleClient();
}

}  // namespace reconclave
