#include "node_service.h"

#include <WiFi.h>

#include "reconclave/protocol.h"  // shared domain library (proves fleet integration)
#include "reconclave/identity.h"
#include "framework.h"            // Config
#include "radio_manager.h"
#include "status_led.h"
#include "storage_service.h"
#include "ducky_script.h"

namespace reconclave {
namespace {
constexpr const char* kFirmware = "0.2.0-dual.1";
constexpr const char* kDeviceType = "t-dongle-s3";
constexpr size_t kMaxBodyBytes = reconclave::kMaxPayloadBytes;  // 16 KiB
}  // namespace

void NodeService::beginIdentity(Config& config) {
  (void)config;
  String mac = WiFi.macAddress();  // "AA:BB:CC:DD:EE:FF"
  mac.replace(":", "");
  mac.toLowerCase();
  device_id_ = "rc-tdongle-" + mac;
  trust_.begin();
  Serial.printf("node: id=%s boot_nonce=%s\n", device_id_.c_str(), trust_.bootNonceHex());
}

void NodeService::registerCapability(const String& id, const String& permission,
                                     CapabilityHandler handler) {
  capabilities_.push_back({id, permission, std::move(handler)});
  Serial.printf("node: capability registered: %s (%s)\n", id.c_str(), permission.c_str());
}

const NodeService::Registered* NodeService::find(const String& id) const {
  for (const auto& c : capabilities_) {
    if (c.id == id) return &c;
  }
  return nullptr;
}

void NodeService::startServer(StatusLed& led, Config& config, RadioManager& radio,
                              StorageService& storage) {
  led_ = &led;
  config_ = &config;
  radio_ = &radio;
  storage_ = &storage;
  // Protocol surface.
  server_.on("/reconclave/v1/announce", HTTP_GET, [this]() { handleAnnounce(); });
  server_.on("/reconclave/v1/message", HTTP_POST, [this]() { handleMessage(); });
  // Setup/status web UI (no payload arming).
  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/admin/status", HTTP_GET, [this]() { handleAdminStatus(); });
  server_.on("/admin/wifi", HTTP_POST, [this]() { handleAdminWifi(); });
  server_.on("/api/payloads", HTTP_GET, [this]() { handlePayloads(); });
  server_.on("/api/payload", HTTP_GET, [this]() { handlePayload(); });
  server_.on("/api/payload", HTTP_POST, [this]() { handlePayloadSave(); });
  server_.on("/api/payload/delete", HTTP_POST, [this]() { handlePayloadDelete(); });
  server_.on("/api/payload/arm", HTTP_POST, [this]() { handlePayloadArm(); });
  server_.on("/api/loot", HTTP_GET, [this]() { handleLoot(); });
  server_.on("/api/loot/file", HTTP_GET, [this]() { handleLootFile(); });
  server_.on("/api/loot", HTTP_POST, [this]() { handleLootSave(); });
  server_.on("/api/loot/delete", HTTP_POST, [this]() { handleLootDelete(); });
  server_.on("/api/audit", HTTP_GET, [this]() { handleAudit(); });
  server_.on("/api/backup", HTTP_POST, [this]() { handleBackup(); });
  server_.on("/api/wifi/scan", HTTP_GET, [this]() { handleWifiScan(); });
  server_.onNotFound([this]() { server_.send(404, "application/json", "{\"error\":\"not found\"}"); });
  server_.begin();
  Serial.println("node: HTTP server up (/, /admin/*, /reconclave/v1/*)");
}

void NodeService::handleAnnounce() {
  JsonDocument doc;
  doc["proto"] = reconclave::kProtocolVersion;
  doc["type"] = "announce";
  doc["message_id"] = device_id_ + "-" + String((uint32_t)millis());
  doc["source_node"] = device_id_;
  doc["timestamp_ms"] = (uint64_t)millis();
  doc["sequence"] = sequence_++;

  JsonObject payload = doc["payload"].to<JsonObject>();
  payload["device_id"] = device_id_;
  payload["device_type"] = kDeviceType;
  payload["firmware"] = kFirmware;
  JsonArray roles = payload["roles"].to<JsonArray>();
  roles.add("node");
  roles.add("standalone");

  JsonArray caps = payload["capabilities"].to<JsonArray>();
  JsonArray descriptors = payload["capability_descriptors"].to<JsonArray>();
  for (const auto& c : capabilities_) {
    caps.add(c.id);
    JsonObject d = descriptors.add<JsonObject>();
    d["id"] = c.id;
    d["permission"] = c.permission;
    JsonObject limits = d["limits"].to<JsonObject>();
    limits["weight"] = 1;
    limits["max_concurrency"] = 1;
  }

  JsonObject resources = payload["resources"].to<JsonObject>();
  resources["network_mbps"] = 20;
  resources["persistent_storage"] = true;
  resources["storage_bytes"] = storage_ != nullptr ? storage_->totalBytes() : 0;
  payload["status"] = "ready";

  JsonObject security = payload["security"].to<JsonObject>();
  security["paired"] = true;
  security["mode"] = "provisioned-hmac-sha256-128";
  security["boot_nonce"] = trust_.bootNonceHex();

  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void NodeService::handleMessage() {
  const String body = server_.arg("plain");
  if (body.length() == 0 || body.length() > kMaxBodyBytes) {
    server_.send(400, "application/json", "{\"error\":\"empty or oversize body\"}");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    server_.send(400, "application/json", "{\"error\":\"invalid JSON\"}");
    return;
  }
  if (String(doc["proto"] | "") != reconclave::kProtocolVersion ||
      String(doc["type"] | "") != "request") {
    server_.send(400, "application/json", "{\"error\":\"unsupported envelope\"}");
    return;
  }

  const String source = doc["source_node"] | "";
  const String destination = doc["destination_node"] | "";
  if (destination.length() != 0 && destination != device_id_) {
    server_.send(400, "application/json", "{\"error\":\"wrong destination\"}");
    return;
  }

  JsonObjectConst payload = doc["payload"].as<JsonObjectConst>();
  const String capability = payload["capability"] | "";
  const String request_id = payload["request_id"] | "";
  JsonVariantConst arguments = payload["arguments"];
  JsonObjectConst auth = payload["auth"].as<JsonObjectConst>();

  RequestContext ctx;
  ctx.source_node = source;
  ctx.request_id = request_id;
  ctx.capability = capability;
  ctx.authenticated = false;

  CapabilityOutcome outcome;
  const Registered* reg = find(capability);
  if (reg == nullptr) {
    outcome.status = "rejected";
    outcome.error_code = "CAPABILITY_UNAVAILABLE";
    outcome.error_message = "capability not offered by this node";
  } else {
    bool authed = true;
    if (reg->permission != "public") {
      String reason;
      const rc_provisioned_peer_t* peer =
          trust_.verifyRequest(source.c_str(), device_id_.c_str(), request_id.c_str(),
                               capability.c_str(), arguments, auth, reason);
      authed = peer != nullptr;
      if (!authed) {
        Serial.printf("node: request rejected (%s): %s\n", capability.c_str(), reason.c_str());
        outcome.status = "rejected";
        outcome.error_code = "UNAUTHORIZED";
        outcome.error_message = reason;
        if (led_) led_->flash(CRGB(255, 170, 28), 250);  // amber
      }
    }
    if (authed) {
      ctx.authenticated = true;
      outcome = reg->handler(arguments, ctx);
      if (led_) {
        led_->flash(outcome.status == "accepted" || outcome.status == "ok" ? CRGB(0, 205, 215)
                                                                           : CRGB(255, 170, 28),
                    250);
      }
    }
  }

  // Build the response envelope (unsigned in milestone 1; response signing is m2).
  JsonDocument resp;
  resp["proto"] = reconclave::kProtocolVersion;
  resp["type"] = "response";
  resp["message_id"] = device_id_ + "-" + String((uint32_t)millis());
  resp["source_node"] = device_id_;
  resp["destination_node"] = source;
  resp["timestamp_ms"] = (uint64_t)millis();
  JsonObject rp = resp["payload"].to<JsonObject>();
  rp["request_id"] = request_id;
  rp["status"] = outcome.status;
  if (outcome.error_code.length()) rp["error_code"] = outcome.error_code;
  if (outcome.error_message.length()) rp["error_message"] = outcome.error_message;
  JsonDocument result;
  if (deserializeJson(result, outcome.result_json) == DeserializationError::Ok) {
    rp["result"] = result.as<JsonVariantConst>();
  }

  String out;
  serializeJson(resp, out);
  server_.send(200, "application/json", out);
}

namespace {
// Single-file control deck: payload authoring and arming are permitted over the
// local management network, but execution remains a physical-button action.
const char kSetupPage[] = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Reconclave Dongle</title>
<style>:root{color-scheme:dark;--c:#050a10;--s:#0e222e;--r:#16303f;--sel:#164248;--line:#237670;--a:#00cdd7;--w:#ffaa1c;--d:#ff4f87;--ink:#fff2d7;--muted:#c9b896}*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 80% 0,#12374755,transparent 35%),var(--c);color:var(--ink);font:14px system-ui,sans-serif}header{height:64px;border-bottom:1px solid var(--line);background:#0e222eee;display:flex;align-items:center;padding:0 max(18px,calc((100% - 1100px)/2));gap:13px;position:sticky;top:0;z-index:2}.mark{width:36px;height:36px;border:1px solid var(--a);border-radius:50%;display:grid;place-items:center;color:var(--a);font:700 20px monospace;box-shadow:0 0 18px #00cdd733}.brand b{display:block;letter-spacing:.12em}.brand small,.muted{color:var(--muted)}.mode{margin-left:auto;color:var(--a);font:11px monospace;border:1px solid var(--line);padding:7px 9px}main{max-width:1100px;margin:auto;padding:22px}.hero{display:grid;grid-template-columns:2fr repeat(3,1fr);gap:10px;margin-bottom:16px}.tile,.panel{background:linear-gradient(145deg,var(--s),#091720);border:1px solid #23767077;padding:16px}.tile strong{display:block;font:20px monospace;color:var(--a);margin-top:7px}.tile small,label{color:var(--muted);font-size:11px}.tabs{display:flex;gap:4px;margin-bottom:10px}.tabs button{background:transparent;border:1px solid transparent;color:var(--muted)}.tabs button.on{color:var(--a);border-color:var(--line);background:#00cdd70d}.view{display:none}.view.on{display:grid;grid-template-columns:280px 1fr;gap:10px}.panel h2{font-size:14px;margin:0 0 14px}.list{display:grid;gap:5px}.item{display:flex;gap:8px;align-items:center;border:1px solid transparent;background:#050a1044;color:var(--ink);padding:10px;text-align:left;width:100%}.item:hover,.item.on{border-color:var(--line);background:var(--sel)}.item span{flex:1}.armed{color:var(--w);font:10px monospace}input,textarea,select{width:100%;background:var(--c);color:var(--ink);border:1px solid #23767088;padding:10px;font:12px monospace;outline:none}textarea{min-height:300px;resize:vertical}input:focus,textarea:focus,select:focus{border-color:var(--a)}label{display:block;margin:11px 0 5px}.actions{display:flex;flex-wrap:wrap;gap:7px;margin-top:10px}button{border:1px solid var(--line);background:#00cdd710;color:var(--a);padding:9px 12px;cursor:pointer}.primary{background:var(--a);color:var(--c);font-weight:700}.warn{color:var(--w);border-color:#ffaa1c66}.danger{color:var(--d);border-color:#ff4f8766}.note{border-left:2px solid var(--w);padding:9px 11px;background:#ffaa1c0b;color:var(--muted);font-size:11px;line-height:1.5;margin-top:14px}.rows div{display:flex;justify-content:space-between;border-bottom:1px solid #23767044;padding:8px 0}.rows span:first-child{color:var(--muted)}details.builder{border:1px solid #23767066;background:#050a1055;margin-bottom:14px;padding:12px}details.builder summary{cursor:pointer;color:var(--a);font-weight:600}.buildgrid{display:grid;grid-template-columns:1fr 1fr;gap:0 10px}.buildgrid .wide{grid-column:1/-1}.builder textarea{min-height:70px}#toast{position:fixed;right:18px;bottom:18px;background:var(--r);border:1px solid var(--a);padding:11px 14px;display:none}@media(max-width:720px){.hero{grid-template-columns:1fr 1fr}.hero .tile:first-child{grid-column:1/-1}.view.on{grid-template-columns:1fr}.buildgrid{grid-template-columns:1fr}.buildgrid .wide{grid-column:auto}header{padding:0 14px}main{padding:14px}}</style></head>
<body><header><div class="mark">Z</div><div class="brand"><b>RECONCLAVE</b><small>T-Dongle control deck</small></div><div class="mode" id="mode">STANDALONE</div></header><main>
<section class="hero"><div class="tile"><small>DEVICE</small><strong id="did">…</strong><span class="muted" id="addr">Loading status</span></div><div class="tile"><small>ARMED</small><strong id="armed">SAFE</strong></div><div class="tile"><small>PAYLOADS</small><strong id="pc">0</strong></div><div class="tile"><small>LOOT</small><strong id="lc">0</strong></div></section>
<nav class="tabs"><button class="on" data-v="payloads">Payloads</button><button data-v="loot">Loot</button><button data-v="device">Device</button></nav>
<section id="payloads" class="view on"><div class="panel"><h2>Payload library</h2><div id="plist" class="list"></div><button onclick="newPayload()">+ New payload</button></div><div class="panel"><h2>Payload editor</h2><details class="builder"><summary>Guided payload builder</summary><div class="buildgrid"><div><label>Template</label><select id="bt"><option value="text">Type text safely</option><option value="linux-copy">Linux: copy one file</option><option value="windows-copy">Windows: copy one file</option><option value="linux-command">Linux: terminal command</option></select></div><div><label>Startup delay (ms)</label><input id="bd" type="number" min="0" max="60000" value="2000"></div><div class="wide"><label>Payload name</label><input id="bn" maxlength="40" placeholder="my-guided-payload"></div><div><label>Source / text / command</label><textarea id="bs" placeholder="Text to type or source path"></textarea></div><div><label>Destination</label><textarea id="bx" placeholder="Mounted-volume destination path"></textarea></div></div><div class="actions"><button class="primary" onclick="buildPayload()">Generate in editor</button></div><div class="note">The builder creates editable DuckyScript with `DEFINE` variables and `{{NAME}}` placeholders. Review keyboard layout, paths, timing, and destination before saving or arming.</div></details><label>Name</label><input id="pn" maxlength="40" placeholder="hello-world"><label>DuckyScript</label><textarea id="pb" spellcheck="false" placeholder="DEFINE MESSAGE Hello from Reconclave&#10;STRING {{MESSAGE}}"></textarea><div class="actions"><button class="primary" onclick="savePayload()">Save</button><button class="warn" onclick="armPayload()">Arm for button</button><button onclick="disarm()">Disarm</button><button class="danger" onclick="deletePayload()">Delete</button></div><div class="note"><b>Physical trigger only.</b> This console can author and arm a standalone payload, but cannot run it. Pressing the Dongle button is the explicit execution gesture. Fleet-triggered actions separately require authenticated scope.</div></div></section>
<section id="loot" class="view"><div class="panel"><h2>Loot folder</h2><div id="llist" class="list"></div></div><div class="panel"><h2>Loot viewer</h2><label>Name</label><input id="ln" maxlength="40" placeholder="session-note"><label>Contents</label><textarea id="lb" spellcheck="false" placeholder="Stored output or operator notes"></textarea><div class="actions"><button class="primary" onclick="saveLoot()">Save</button><button class="danger" onclick="deleteLoot()">Delete</button></div><div class="note">Loot is stored locally in LittleFS. Host-mounted mass storage is not enabled yet; filesystem ownership must be switched safely before MSC can be exposed.</div></div></section>
<section id="device" class="view"><div class="panel"><h2>Node status</h2><div class="rows"><div><span>Fleet link</span><span id="link">…</span></div><div><span>STA address</span><span id="staip">…</span></div><div><span>Management AP</span><span id="ap">…</span></div><div><span>AP address</span><span id="apip">…</span></div><div><span>Runs / failures</span><span id="runs">…</span></div><div><span>Internal storage</span><span id="storage">…</span></div><div><span>SD card</span><span id="sd">…</span></div><div><span>Free heap</span><span id="heap">…</span></div><div><span>Firmware</span><span id="fw">…</span></div></div><div class="actions"><button onclick="backup()">Back up to SD</button><button onclick="audit()">View audit log</button></div></div><div class="panel"><h2>Fleet Wi-Fi</h2><label>SSID</label><input id="s"><label>Password</label><input id="p" type="password"><div class="actions"><button onclick="scanWifi()">Scan networks</button><button class="primary" onclick="saveWifi()">Save and reboot</button></div><div id="nets" class="list"></div><label>Audit log</label><textarea id="audit" readonly placeholder="Load the bounded device audit trail"></textarea><div class="note">The management AP remains available when the fleet network is offline. Long-press the physical button to disarm immediately.</div></div></section></main><div id="toast"></div>
<script>let selected='',loot='';const $=id=>document.getElementById(id);async function api(url,opt){let r=await fetch(url,opt),d=await r.json();if(!r.ok)throw Error(d.error||'Request failed');return d}function post(url,data){return api(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})}function toast(s){$('toast').textContent=s;$('toast').style.display='block';setTimeout(()=>$('toast').style.display='none',1800)}document.querySelectorAll('.tabs button').forEach(b=>b.onclick=()=>{document.querySelectorAll('.tabs button,.view').forEach(x=>x.classList.remove('on'));b.classList.add('on');$(b.dataset.v).classList.add('on')});async function status(){let d=await api('/admin/status');$('did').textContent=d.device_id;$('link').textContent=d.sta_link?'ONLINE':'OFFLINE';$('mode').textContent=d.sta_link?'NODE + STANDALONE':'STANDALONE';$('staip').textContent=d.sta_ip;$('ap').textContent=d.ap_ssid;$('apip').textContent=d.ap_ip;$('addr').textContent=d.ap_ssid+' · '+d.ap_ip;$('armed').textContent=d.armed||'SAFE';$('runs').textContent=d.run_count+' / '+d.failure_count;$('storage').textContent=Math.round(d.storage_used/1024)+'K / '+Math.round(d.storage_total/1024)+'K';$('sd').textContent=d.sd_available?Math.round(d.sd_used/1048576)+'M / '+Math.round(d.sd_total/1048576)+'M':'NOT PRESENT';$('heap').textContent=Math.round(d.free_heap/1024)+'K';$('fw').textContent=d.firmware}async function lists(){let p=await api('/api/payloads'),l=await api('/api/loot');$('pc').textContent=p.payloads.length;$('lc').textContent=l.loot.length;$('plist').innerHTML=p.payloads.map(n=>`<button class="item ${n==p.armed?'on':''}" onclick="openPayload('${n}')"><span>${n}</span>${n==p.armed?'<b class="armed">ARMED</b>':''}</button>`).join('')||'<span class="muted">No saved payloads</span>';$('llist').innerHTML=l.loot.map(n=>`<button class="item" onclick="openLoot('${n}')"><span>${n}</span></button>`).join('')||'<span class="muted">Loot folder empty</span>'}async function openPayload(n){let d=await api('/api/payload?name='+encodeURIComponent(n));selected=n;$('pn').value=n;$('pb').value=d.body}function newPayload(){selected='';$('pn').value='';$('pb').value='';$('pn').focus()}async function savePayload(){await post('/api/payload',{name:$('pn').value,body:$('pb').value});selected=$('pn').value;toast('Payload validated and saved');await lists()}async function armPayload(){await post('/api/payload/arm',{name:$('pn').value});toast('Armed for physical button');await refresh()}async function disarm(){await post('/api/payload/arm',{name:''});toast('Dongle disarmed');await refresh()}async function deletePayload(){if(!selected||!confirm('Delete '+selected+'?'))return;await post('/api/payload/delete',{name:selected});newPayload();toast('Payload deleted');await refresh()}async function openLoot(n){let d=await api('/api/loot/file?name='+encodeURIComponent(n));loot=n;$('ln').value=n;$('lb').value=d.body}async function saveLoot(){await post('/api/loot',{name:$('ln').value,body:$('lb').value});loot=$('ln').value;toast('Loot saved');await lists()}async function deleteLoot(){if(!loot||!confirm('Delete '+loot+'?'))return;await post('/api/loot/delete',{name:loot});loot='';$('ln').value='';$('lb').value='';toast('Loot deleted');await lists()}async function backup(){try{await post('/api/backup',{});toast('Backup written to SD')}catch(e){toast(e.message)}}async function audit(){let d=await api('/api/audit');$('audit').value=d.log}async function scanWifi(){let d=await api('/api/wifi/scan');$('nets').innerHTML=d.networks.map(n=>`<button class="item" onclick="$('s').value=this.firstChild.textContent"><span>${n.ssid}</span><b class="armed">${n.rssi} dBm ${n.secure?'LOCK':''}</b></button>`).join('')||'<span class="muted">No networks found</span>'}async function saveWifi(){await post('/admin/wifi',{ssid:$('s').value,pass:$('p').value});toast('Saved; rebooting')}async function refresh(){try{await Promise.all([status(),lists()])}catch(e){toast(e.message)}}refresh();setInterval(status,5000)</script>
<script>function oneLine(v){return v.replace(/[\r\n]+/g,' ').trim()}function buildPayload(){let t=$('bt').value,d=Math.max(0,Math.min(60000,Number($('bd').value)||0)),src=oneLine($('bs').value),dst=oneLine($('bx').value),lines=['REM Generated by Reconclave guided builder','REM Review paths, keyboard layout, and timing before arming'];if(t==='text'){lines.push('DEFINE MESSAGE '+src,'DELAY '+d,'STRINGLN {{MESSAGE}}')}else if(t==='linux-copy'){lines.push('DEFINE SOURCE '+src,'DEFINE DESTINATION '+dst,'DELAY '+d,'CTRL ALT t','DELAY 700','STRINGLN cp -- "{{SOURCE}}" "{{DESTINATION}}"','DELAY 400','STRINGLN sync')}else if(t==='windows-copy'){lines.push('DEFINE SOURCE '+src,'DEFINE DESTINATION '+dst,'DELAY '+d,'GUI r','DELAY 500','STRING powershell -NoProfile -Command "Copy-Item -LiteralPath \'{{SOURCE}}\' -Destination \'{{DESTINATION}}\'"','ENTER')}else{lines.push('DEFINE COMMAND '+src,'DELAY '+d,'CTRL ALT t','DELAY 700','STRINGLN {{COMMAND}}')}$('pn').value=$('bn').value||'guided-payload';$('pb').value=lines.join('\n')+'\n';selected='';toast('Generated; review before saving')}</script>
</body></html>)HTML";
}  // namespace

void NodeService::handleRoot() {
  // Keep the self-contained page cheap to store in flash while still sourcing
  // its canonical theme values from the generated identity header.
  auto cssHex = [](uint32_t color) {
    char value[8];
    snprintf(value, sizeof(value), "#%06lx", static_cast<unsigned long>(color));
    return String(value);
  };
  String page(kSetupPage);
  page.replace("#050a10", cssHex(identity::kColorCanvas));
  page.replace("#0e222e", cssHex(identity::kColorSurface));
  page.replace("#16303f", cssHex(identity::kColorSurfaceRaised));
  page.replace("#164248", cssHex(identity::kColorSurfaceSelected));
  page.replace("#237670", cssHex(identity::kColorBorder));
  page.replace("#00cdd7", cssHex(identity::kColorAccent));
  page.replace("#ffaa1c", cssHex(identity::kColorWarning));
  page.replace("#ff4f87", cssHex(identity::kColorDanger));
  page.replace("#fff2d7", cssHex(identity::kColorInk));
  page.replace("#c9b896", cssHex(identity::kColorInkMuted));
  server_.send(200, "text/html; charset=utf-8", page);
}

void NodeService::handleAdminStatus() {
  JsonDocument doc;
  doc["device_id"] = device_id_;
  doc["sta_link"] = radio_ != nullptr && radio_->staConnected();
  doc["sta_ip"] = radio_ != nullptr ? radio_->staIp().toString() : String("0.0.0.0");
  doc["ap_ssid"] = radio_ != nullptr ? radio_->apSsid() : String("");
  doc["ap_ip"] = radio_ != nullptr ? radio_->apIp().toString() : String("0.0.0.0");
  JsonArray caps = doc["capabilities"].to<JsonArray>();
  for (const auto& c : capabilities_) caps.add(c.id);
  doc["armed"] = storage_ != nullptr ? storage_->armedPayload() : String("");
  doc["run_count"] = storage_ != nullptr ? storage_->runCount() : 0;
  doc["failure_count"] = storage_ != nullptr ? storage_->failureCount() : 0;
  doc["storage_used"] = storage_ != nullptr ? storage_->usedBytes() : 0;
  doc["storage_total"] = storage_ != nullptr ? storage_->totalBytes() : 0;
  doc["sd_available"] = storage_ != nullptr && storage_->sdAvailable();
  doc["sd_used"] = storage_ != nullptr ? storage_->sdUsedBytes() : 0;
  doc["sd_total"] = storage_ != nullptr ? storage_->sdTotalBytes() : 0;
  doc["uptime_ms"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["firmware"] = kFirmware;
  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void NodeService::handlePayloads() {
  JsonDocument doc;
  doc["armed"] = storage_->armedPayload();
  JsonArray items = doc["payloads"].to<JsonArray>();
  for (const String& name : storage_->listPayloads()) items.add(name);
  String out; serializeJson(doc, out); server_.send(200, "application/json", out);
}

void NodeService::handlePayload() {
  String body;
  if (!server_.hasArg("name") || !storage_->loadPayload(server_.arg("name"), body)) {
    server_.send(404, "application/json", "{\"error\":\"payload not found\"}"); return;
  }
  JsonDocument doc; doc["name"] = server_.arg("name"); doc["body"] = body;
  String out; serializeJson(doc, out); server_.send(200, "application/json", out);
}

void NodeService::handlePayloadSave() {
  if (server_.arg("plain").length() > StorageService::kMaxPayloadBytes + 256) {
    server_.send(413, "application/json", "{\"error\":\"payload too large\"}"); return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain"))) {
    server_.send(400, "application/json", "{\"error\":\"invalid JSON\"}"); return;
  }
  String error;
  const String script = doc["body"] | "";
  const DuckyScriptResult validation = validateDuckyScript(script);
  if (!validation.ok) {
    JsonDocument reply;
    reply["error"] = "line " + String(validation.line_number) + ": " + validation.error;
    String out; serializeJson(reply, out); server_.send(400, "application/json", out); return;
  }
  if (!storage_->savePayload(doc["name"] | "", script, error)) {
    JsonDocument reply; reply["error"] = error; String out; serializeJson(reply, out);
    server_.send(400, "application/json", out); return;
  }
  storage_->appendAudit("payload-save", doc["name"] | "");
  server_.send(200, "application/json", "{\"ok\":true}");
}

void NodeService::handlePayloadDelete() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain")) || !storage_->removePayload(doc["name"] | "")) {
    server_.send(400, "application/json", "{\"error\":\"delete failed\"}"); return;
  }
  storage_->appendAudit("payload-delete", doc["name"] | "");
  server_.send(200, "application/json", "{\"ok\":true}");
}

void NodeService::handlePayloadArm() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain")) || !storage_->armPayload(doc["name"] | "")) {
    server_.send(400, "application/json", "{\"error\":\"unknown payload\"}"); return;
  }
  storage_->appendAudit("payload-arm", String(doc["name"] | "").length() ? String(doc["name"] | "") : String("safe"));
  server_.send(200, "application/json", "{\"ok\":true}");
}

void NodeService::handleLoot() {
  JsonDocument doc; JsonArray items = doc["loot"].to<JsonArray>();
  for (const String& name : storage_->listLoot()) items.add(name);
  String out; serializeJson(doc, out); server_.send(200, "application/json", out);
}

void NodeService::handleLootFile() {
  String body;
  if (!server_.hasArg("name") || !storage_->loadLoot(server_.arg("name"), body)) {
    server_.send(404, "application/json", "{\"error\":\"loot not found\"}"); return;
  }
  JsonDocument doc; doc["name"] = server_.arg("name"); doc["body"] = body;
  String out; serializeJson(doc, out); server_.send(200, "application/json", out);
}

void NodeService::handleLootSave() {
  if (server_.arg("plain").length() > StorageService::kMaxLootBytes + 256) {
    server_.send(413, "application/json", "{\"error\":\"loot too large\"}"); return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain"))) {
    server_.send(400, "application/json", "{\"error\":\"invalid JSON\"}"); return;
  }
  String error;
  if (!storage_->saveLoot(doc["name"] | "", doc["body"] | "", error)) {
    JsonDocument reply; reply["error"] = error; String out; serializeJson(reply, out);
    server_.send(400, "application/json", out); return;
  }
  storage_->appendAudit("loot-save", doc["name"] | "");
  server_.send(200, "application/json", "{\"ok\":true}");
}

void NodeService::handleLootDelete() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain")) || !storage_->removeLoot(doc["name"] | "")) {
    server_.send(400, "application/json", "{\"error\":\"delete failed\"}"); return;
  }
  storage_->appendAudit("loot-delete", doc["name"] | "");
  server_.send(200, "application/json", "{\"ok\":true}");
}

void NodeService::handleAudit() {
  JsonDocument doc; doc["log"] = storage_->auditLog();
  String out; serializeJson(doc, out); server_.send(200, "application/json", out);
}

void NodeService::handleBackup() {
  String error;
  if (!storage_->backupToSd(error)) {
    JsonDocument doc; doc["error"] = error; String out; serializeJson(doc, out);
    server_.send(409, "application/json", out); return;
  }
  server_.send(200, "application/json", "{\"ok\":true}");
}

void NodeService::handleWifiScan() {
  const int found = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);
  JsonDocument doc; JsonArray networks = doc["networks"].to<JsonArray>();
  const int limit = found < 20 ? found : 20;
  for (int i = 0; i < limit; ++i) {
    JsonObject network = networks.add<JsonObject>();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    network["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  String out; serializeJson(doc, out); server_.send(200, "application/json", out);
}

void NodeService::handleAdminWifi() {
  const String body = server_.arg("plain");
  JsonDocument doc;
  if (body.length() == 0 || deserializeJson(doc, body)) {
    server_.send(400, "application/json", "{\"error\":\"invalid JSON\"}");
    return;
  }
  const String ssid = doc["ssid"] | "";
  const String pass = doc["pass"] | "";
  if (ssid.length() == 0 || config_ == nullptr) {
    server_.send(400, "application/json", "{\"error\":\"ssid required\"}");
    return;
  }
  config_->setString("wifi_ssid", ssid);
  config_->setString("wifi_pass", pass);
  server_.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
  Serial.printf("admin: wifi set to \"%s\" via web UI - rebooting to join\n", ssid.c_str());
  delay(300);
  ESP.restart();
}

void NodeService::handleClient() { server_.handleClient(); }

}  // namespace reconclave
