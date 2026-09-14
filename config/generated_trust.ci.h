#pragma once
#include <stdint.h>

// Disposable compile-only values. Never provision hardware with this file.
typedef struct {
  const char *peer_id;
  uint8_t priority;
  uint8_t key[32];
} rc_provisioned_peer_t;
static const rc_provisioned_peer_t RC_PROVISIONED_PEERS[] = {
  {"ci-command", 100, {0}},
};
