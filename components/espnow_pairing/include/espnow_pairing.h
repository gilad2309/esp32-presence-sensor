#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_now.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESPNOW_PAIRING_NETID_LEN 8

// ESP-NOW hard-caps encrypted peers at 6 (ESP_NOW_MAX_ENCRYPT_PEER_NUM in
// esp_now.h, confirmed against the installed IDF v5.5.3 headers -- do not
// raise this without checking that constant first).
#define ESPNOW_PAIRING_MAX_SENDERS 6

typedef struct {
    uint8_t magic[4];
    uint8_t network_id[ESPNOW_PAIRING_NETID_LEN];
    uint8_t role;   // 0 = hello (sender), 1 = reply (receiver)
} __attribute__((packed)) espnow_pairing_msg_t;

// Shared setup, called internally by both variants below: registers the
// broadcast peer and the ESP-NOW recv callback dispatcher. Not called
// directly by application code -- use one of the two init functions below.
void espnow_pairing_init(const uint8_t network_id[ESPNOW_PAIRING_NETID_LEN],
                          const uint8_t lmk[16],
                          esp_now_recv_cb_t app_recv_cb);

// --- Sender side --- (remembers exactly one peer: the receiver)
// Checks NVS for a cached peer first (fast path). If none, broadcasts
// hello every 300ms until a receiver replies or timeout_ms elapses.
// On success: registers the peer (encrypted, LR rate), caches its MAC to
// NVS, and fills out_mac. Returns false on timeout -- caller must not
// proceed to esp_now_send() in that case (see sender integration).
bool espnow_pairing_get_receiver(uint32_t timeout_ms, uint8_t out_mac[6]);

// Drops the peer entry and the NVS cache so the next
// espnow_pairing_get_receiver() call re-discovers instead of retrying a
// dead target.
void espnow_pairing_forget_receiver(void);

// --- Receiver side --- (remembers up to ESPNOW_PAIRING_MAX_SENDERS peers)
// Call once at startup instead of espnow_pairing_init() directly. Restores
// every previously-paired sender from NVS immediately (no broadcast wait
// needed for known senders), then listens indefinitely for hellos from new
// ones -- each valid one is registered as an encrypted peer and persisted
// to NVS so it survives this receiver's own reboots too. If the table is
// already full when a new sender shows up, evicts whoever's gone longest
// without being heard from (LRU) to make room.
void espnow_pairing_receiver_init(const uint8_t network_id[ESPNOW_PAIRING_NETID_LEN],
                                   const uint8_t lmk[16],
                                   esp_now_recv_cb_t app_recv_cb);

// Clears every persisted sender from NVS and reboots (simplest reliable way
// to also clear their in-RAM esp_now peer entries). Intended to be wired up
// to a manual trigger (e.g. hold-BOOT-10s), and doubles as recovery if the
// sender table ever fills with junk.
void espnow_pairing_forget_all_senders(void);

#ifdef __cplusplus
}
#endif
