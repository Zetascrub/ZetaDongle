#include "trust.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <iterator>
#include <string>

#include "esp_random.h"
#include "mbedtls/md.h"

namespace reconclave {
namespace {

constexpr size_t kMaxSeenNonces = 64;
// Lease bounds are a sanity gate only; the exact lease_ms is folded into the
// HMAC canonical string, so authenticity does not depend on these values.
constexpr long kLeaseMinMs = 1000;
constexpr long kLeaseMaxMs = 60000;

void toHex(const uint8_t* bytes, size_t len, char* out) {
  static const char* d = "0123456789abcdef";
  for (size_t i = 0; i < len; ++i) {
    out[i * 2] = d[bytes[i] >> 4];
    out[i * 2 + 1] = d[bytes[i] & 0x0f];
  }
  out[len * 2] = '\0';
}

bool hexDecode(const char* hex, uint8_t* out, size_t out_len) {
  if (strlen(hex) != out_len * 2) return false;
  for (size_t i = 0; i < out_len; ++i) {
    unsigned v = 0;
    if (sscanf(hex + i * 2, "%2x", &v) != 1) return false;
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}

void hmacSha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len,
                uint8_t out[32]) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, key, key_len, msg, msg_len, out);
}

// Constant-time compare over the fixed tag length.
bool ctEqual(const uint8_t* a, const uint8_t* b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; ++i) diff |= a[i] ^ b[i];
  return diff == 0;
}

}  // namespace

void Trust::begin() {
  uint8_t nonce[16];
  esp_fill_random(nonce, sizeof(nonce));
  toHex(nonce, sizeof(nonce), boot_nonce_hex_);
}

bool Trust::provisioned() const {
  for (const auto& peer : RC_PROVISIONED_PEERS) {
    if (peer.peer_id == nullptr || peer.peer_id[0] == '\0') continue;
    if (std::any_of(std::begin(peer.key), std::end(peer.key),
                    [](uint8_t byte) { return byte != 0; })) return true;
  }
  return false;
}

String Trust::sha256Hex(const String& text) {
  uint8_t digest[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md(info, reinterpret_cast<const uint8_t*>(text.c_str()), text.length(), digest);
  char hex[65];
  toHex(digest, sizeof(digest), hex);
  return String(hex);
}

// Hosted scopes sort object keys; the coordinator's outer request and response
// signatures keep insertion order. Both use compact JSON.
bool Trust::canonicalJson(JsonVariantConst value, String& out, const char* exclude_key,
                          bool sort_keys) {
  if (value.is<JsonObjectConst>()) {
    JsonObjectConst obj = value.as<JsonObjectConst>();
    std::vector<std::string> keys;
    for (JsonPairConst kv : obj) {
      const char* k = kv.key().c_str();
      if (exclude_key != nullptr && strcmp(k, exclude_key) == 0) continue;
      for (const char* c = k; *c != '\0'; ++c) {
        const bool safe = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                          (*c >= '0' && *c <= '9') || *c == '_';
        if (!safe) return false;
      }
      keys.emplace_back(k);
    }
    if (sort_keys)
      std::sort(keys.begin(), keys.end(),
                [](const std::string& a, const std::string& b) { return a < b; });
    out += "{";
    for (size_t i = 0; i < keys.size(); ++i) {
      if (i > 0) out += ",";
      out += "\"";
      out += keys[i].c_str();
      out += "\":";
      if (!canonicalJson(obj[keys[i].c_str()], out, nullptr, sort_keys)) return false;
    }
    out += "}";
    return true;
  }
  if (value.is<JsonArrayConst>()) {
    out += "[";
    bool first = true;
    for (JsonVariantConst item : value.as<JsonArrayConst>()) {
      if (!first) out += ",";
      first = false;
      if (!canonicalJson(item, out, nullptr, sort_keys)) return false;
    }
    out += "]";
    return true;
  }
  // Leaf (null / bool / integer / string): ArduinoJson emits the same compact
  // JSON form Python does for these types.
  String leaf;
  serializeJson(value, leaf);
  out += leaf;
  return true;
}

bool Trust::nonceSeenOrRecord(uint64_t nonce) {
  if (std::find(seen_nonces_.begin(), seen_nonces_.end(), nonce) != seen_nonces_.end()) {
    return true;
  }
  if (seen_nonces_.size() >= kMaxSeenNonces) seen_nonces_.erase(seen_nonces_.begin());
  seen_nonces_.push_back(nonce);
  return false;
}

const rc_provisioned_peer_t* Trust::verifyRequest(const char* source_id, const char* dest_id,
                                                  const char* request_id, const char* capability,
                                                  JsonVariantConst arguments, JsonObjectConst auth,
                                                  String& reason) {
  // Locate the provisioned peer.
  const rc_provisioned_peer_t* peer = nullptr;
  for (const auto& candidate : RC_PROVISIONED_PEERS) {
    if (strcmp(candidate.peer_id, source_id) == 0) {
      peer = &candidate;
      break;
    }
  }
  if (peer == nullptr) {
    reason = "unknown peer";
    return nullptr;
  }
  if (std::all_of(std::begin(peer->key), std::end(peer->key),
                  [](uint8_t byte) { return byte == 0; })) {
    reason = "peer is not provisioned";
    return nullptr;
  }

  const char* tag_hex = auth["tag"] | "";
  const char* nonce_hex = auth["nonce"] | "";
  // Field is named "payload_digest" on the wire; its content is the digest of
  // the request arguments (reconclave_node.py / poe-p4 main.c).
  const char* digest_hex = auth["payload_digest"] | "";
  if (!auth["coordinator_priority"].is<int>() || !auth["lease_ms"].is<long>() ||
      tag_hex[0] == '\0' || nonce_hex[0] == '\0' || digest_hex[0] == '\0') {
    reason = "malformed auth block";
    return nullptr;
  }
  const int priority = auth["coordinator_priority"].as<int>();
  const long lease_ms = auth["lease_ms"].as<long>();
  if (priority != peer->priority || lease_ms < kLeaseMinMs || lease_ms > kLeaseMaxMs) {
    reason = "priority/lease out of bounds";
    return nullptr;
  }

  // Recompute and compare the argument digest.
  String canon;
  if (!canonicalJson(arguments, canon, nullptr, false)) {
    reason = "arguments not canonicalisable";
    return nullptr;
  }
  if (sha256Hex(canon) != String(digest_hex)) {
    reason = "argument digest mismatch";
    return nullptr;
  }

  // Rebuild the canonical string and compare the truncated HMAC.
  String message = String(source_id) + "|" + dest_id + "|" + request_id + "|" + capability + "|" +
                   boot_nonce_hex_ + "|" + digest_hex + "|" + nonce_hex + "|" + String(priority) +
                   "|" + String(lease_ms);
  uint8_t full[32];
  hmacSha256(peer->key, 32, reinterpret_cast<const uint8_t*>(message.c_str()), message.length(),
             full);
  uint8_t supplied[kTagBytes];
  if (!hexDecode(tag_hex, supplied, kTagBytes) || !ctEqual(full, supplied, kTagBytes)) {
    reason = "bad tag";
    return nullptr;
  }

  // Replay resistance: the request nonce must be fresh for this boot session.
  char* end = nullptr;
  const uint64_t nonce = strtoull(nonce_hex, &end, 16);
  if (end == nonce_hex || *end != '\0' || nonceSeenOrRecord(nonce)) {
    reason = "replayed or malformed nonce";
    return nullptr;
  }
  return peer;
}

bool Trust::verifyHostedScope(const rc_provisioned_peer_t* peer, JsonVariantConst arguments,
                              const char* capability, const String& device_id,
                              const String& instance_id, String& reason) {
  JsonVariantConst token = arguments["_scope_delegation"];
  if (peer == nullptr || !token.is<JsonObjectConst>() || instance_id.isEmpty()) {
    reason = "signed hosted scope required";
    return false;
  }
  uint8_t supplied[32];
  const char* tag = token["tag"] | "";
  String canonical;
  if (!hexDecode(tag, supplied, sizeof(supplied)) ||
      !canonicalJson(token, canonical, "tag")) {
    reason = "invalid hosted scope signature";
    return false;
  }
  uint8_t expected[32];
  constexpr char kScopeDomain[] = "reconclave-hosted-hid-scope-v1";
  uint8_t scope_key[32];
  hmacSha256(peer->key, sizeof(peer->key),
             reinterpret_cast<const uint8_t*>(kScopeDomain), sizeof(kScopeDomain) - 1, scope_key);
  hmacSha256(scope_key, sizeof(scope_key),
             reinterpret_cast<const uint8_t*>(canonical.c_str()), canonical.length(), expected);
  if (!ctEqual(expected, supplied, sizeof(expected))) {
    reason = "invalid hosted scope signature";
    return false;
  }
  const String host = arguments["host"] | "";
  IPAddress address;
  JsonArrayConst networks = token["included_networks"].as<JsonArrayConst>();
  JsonArrayConst excluded = token["excluded_networks"].as<JsonArrayConst>();
  JsonArrayConst classes = token["capability_classes"].as<JsonArrayConst>();
  if (!address.fromString(host) || host != address.toString() ||
      networks.size() != 1 || String(networks[0] | "") != host + "/32" ||
      excluded.size() != 0 || classes.size() != 1 || String(classes[0] | "") != "capture" ||
      String(token["scope_id"] | "") != "hosted:" + instance_id ||
      String(token["project_id"] | "") != instance_id ||
      String(token["source_node"] | "") != peer->peer_id ||
      String(token["capability"] | "") != capability ||
      String(token["destination_node"] | "") != device_id ||
      String(token["host"] | "") != host) {
    reason = "hosted scope does not match this device and computer";
    return false;
  }
  String original;
  if (!canonicalJson(arguments, original, "_scope_delegation") ||
      String(token["arguments_digest"] | "") != sha256Hex(original)) {
    reason = "hosted scope does not match this script";
    return false;
  }
  const int64_t issued = token["issued_at_ms"] | static_cast<int64_t>(0);
  const int64_t expires = token["expires_at_ms"] | static_cast<int64_t>(0);
  const int64_t now = static_cast<int64_t>(time(nullptr)) * 1000;
  if (now < 1700000000000LL || issued <= 0 || expires <= issued ||
      expires - issued > 60000 || now < issued || now >= expires) {
    reason = "hosted scope has expired or device clock is unavailable";
    return false;
  }
  return true;
}

bool Trust::signResponse(const rc_provisioned_peer_t* peer, const char* source_id,
                         const char* dest_id, const char* request_id, const char* status,
                         const char* nonce_hex, JsonVariantConst body, JsonObject auth) {
  if (peer == nullptr || nonce_hex == nullptr || nonce_hex[0] == '\0') return false;
  String canonical_body;
  if (!canonicalJson(body, canonical_body, nullptr, false)) return false;
  const String digest = sha256Hex(canonical_body);
  const String message = String(source_id) + "|" + dest_id + "|" + request_id + "|" + status +
                         "|" + boot_nonce_hex_ + "|" + digest + "|" + nonce_hex;
  uint8_t full[32];
  hmacSha256(peer->key, 32, reinterpret_cast<const uint8_t*>(message.c_str()),
             message.length(), full);
  char tag[kTagBytes * 2 + 1];
  toHex(full, kTagBytes, tag);
  auth["nonce"] = nonce_hex;
  auth["payload_digest"] = digest;
  auth["tag"] = tag;
  return true;
}

}  // namespace reconclave
