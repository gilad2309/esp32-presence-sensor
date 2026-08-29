#include "espnow_pairing.h"
#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define PAIR_MAGIC          "PAIR"
#define NVS_NAMESPACE        "espnow"
#define NVS_PEER_KEY          "peer_mac"     // sender: single cached receiver MAC
#define NVS_SENDER_CNT_KEY    "sndr_cnt"     // receiver: count of known senders
#define NVS_SENDER_MACS_KEY   "sndr_macs"    // receiver: array of known sender MACs
#define HELLO_INTERVAL_MS     300

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static uint8_t s_network_id[ESPNOW_PAIRING_NETID_LEN];
static uint8_t s_lmk[16];
static esp_now_recv_cb_t s_app_cb;
static bool s_is_receiver = false;   // set true only by espnow_pairing_receiver_init()

// Sender-side discovery state. Semaphore is created once, lazily, and never
// deleted -- avoids a create/delete race against on_recv() running on the
// ESP-NOW driver task (see espnow_pairing_get_receiver()).
static SemaphoreHandle_t s_reply_sem;
static uint8_t s_learned_mac[6];

// Receiver-side: hello replies must never be sent directly from on_recv()
// (it runs on the ESP-NOW/WiFi driver task -- calling esp_now_send() from
// there is not safe). Instead on_recv() schedules this one-shot timer,
// whose callback runs on the separate esp_timer task. A burst of hellos
// from multiple senders discovering at once naturally coalesces into one
// reply: esp_timer_start_once() on an already-pending timer is a no-op.
static esp_timer_handle_t s_reply_timer;

// Receiver-side: in-RAM table of known senders with a last-seen timestamp,
// used to evict the least-recently-seen sender when a new one shows up and
// the table (capped at ESPNOW_PAIRING_MAX_SENDERS = 6, the hard ESP-NOW
// limit) is already full. Not persisted to NVS -- heartbeats arrive every
// few seconds while a sender is present, and writing NVS that often would
// wear out flash fast.
typedef struct {
    uint8_t mac[6];
    bool in_use;
    int64_t last_seen_us;
} sender_slot_t;

static sender_slot_t s_senders[ESPNOW_PAIRING_MAX_SENDERS];

static void set_lr_rate(const uint8_t mac[6])
{
    esp_now_rate_config_t lr_rate = {
        .phymode = WIFI_PHY_MODE_LR,
        .rate    = WIFI_PHY_RATE_LORA_250K,
        .ersu    = false,
        .dcm     = false,
    };
    esp_now_set_peer_rate_config(mac, &lr_rate);
}

static bool add_encrypted_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, mac, 6);
    memcpy(peer.lmk, s_lmk, 16);
    peer.channel = 0;
    peer.encrypt = true;
    if (esp_now_add_peer(&peer) != ESP_OK) return false;
    set_lr_rate(mac);
    return true;
}

static bool load_cached_peer(uint8_t out_mac[6])
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 6;
    esp_err_t err = nvs_get_blob(h, NVS_PEER_KEY, out_mac, &len);
    nvs_close(h);
    return err == ESP_OK && len == 6;
}

static void save_cached_peer(const uint8_t mac[6])
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_PEER_KEY, mac, 6);
    nvs_commit(h);
    nvs_close(h);
}

// --- Receiver-side multi-sender persistence + LRU eviction ---

static int find_sender_slot(const uint8_t mac[6])
{
    for (int i = 0; i < ESPNOW_PAIRING_MAX_SENDERS; i++) {
        if (s_senders[i].in_use && memcmp(s_senders[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

// Call whenever we hear *anything* from a known sender -- not just pairing
// hellos, which only happen once. Real presence-data heartbeats are what
// actually keep a sender looking "recently seen" for the lifetime of a
// pairing, since a cached sender never re-broadcasts a hello.
static void touch_sender(const uint8_t mac[6])
{
    int i = find_sender_slot(mac);
    if (i >= 0) s_senders[i].last_seen_us = esp_timer_get_time();
}

// Rewrites NVS from the current in-RAM table in one shot -- simpler and
// less error-prone than trying to keep two separate representations
// (in-RAM table, NVS blob) in sync field-by-field.
static void persist_senders(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;

    uint8_t count = 0;
    uint8_t macs[ESPNOW_PAIRING_MAX_SENDERS][6];
    for (int i = 0; i < ESPNOW_PAIRING_MAX_SENDERS; i++) {
        if (s_senders[i].in_use) {
            memcpy(macs[count], s_senders[i].mac, 6);
            count++;
        }
    }
    nvs_set_u8(h, NVS_SENDER_CNT_KEY, count);
    nvs_set_blob(h, NVS_SENDER_MACS_KEY, macs, count * 6);
    nvs_commit(h);
    nvs_close(h);
}

static void load_known_senders(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;

    uint8_t count = 0;
    if (nvs_get_u8(h, NVS_SENDER_CNT_KEY, &count) != ESP_OK) { nvs_close(h); return; }
    if (count > ESPNOW_PAIRING_MAX_SENDERS) count = ESPNOW_PAIRING_MAX_SENDERS;

    uint8_t macs[ESPNOW_PAIRING_MAX_SENDERS][6];
    size_t len = sizeof(macs);
    if (nvs_get_blob(h, NVS_SENDER_MACS_KEY, macs, &len) == ESP_OK) {
        int64_t now = esp_timer_get_time();
        for (uint8_t i = 0; i < count; i++) {
            add_encrypted_peer(macs[i]);
            memcpy(s_senders[i].mac, macs[i], 6);
            s_senders[i].in_use = true;
            s_senders[i].last_seen_us = now;   // fresh start on boot
            printf("[PAIRING] Restored sender %02x:%02x:%02x:%02x:%02x:%02x from NVS\n",
                   macs[i][0], macs[i][1], macs[i][2], macs[i][3], macs[i][4], macs[i][5]);
        }
    }
    nvs_close(h);
}

// Registers mac as an encrypted peer. If it's new and the table is already
// full (6 senders, the ESP-NOW hard limit), evicts whichever existing
// sender has gone the longest without being heard from to make room.
static bool add_or_evict_sender(const uint8_t mac[6])
{
    int slot = find_sender_slot(mac);
    if (slot >= 0) {
        add_encrypted_peer(mac);   // no-op if already registered
        touch_sender(mac);
        return true;
    }

    int free_slot = -1, oldest_slot = 0;
    int64_t oldest_ts = INT64_MAX;
    for (int i = 0; i < ESPNOW_PAIRING_MAX_SENDERS; i++) {
        if (!s_senders[i].in_use) { free_slot = i; break; }
        if (s_senders[i].last_seen_us < oldest_ts) {
            oldest_ts = s_senders[i].last_seen_us;
            oldest_slot = i;
        }
    }

    if (free_slot >= 0) {
        slot = free_slot;
    } else {
        slot = oldest_slot;
        printf("[PAIRING] Table full -- evicting %02x:%02x:%02x:%02x:%02x:%02x "
               "(least recently heard from) for new sender\n",
               s_senders[slot].mac[0], s_senders[slot].mac[1], s_senders[slot].mac[2],
               s_senders[slot].mac[3], s_senders[slot].mac[4], s_senders[slot].mac[5]);
        esp_now_del_peer(s_senders[slot].mac);
        s_senders[slot].in_use = false;
    }

    if (!add_encrypted_peer(mac)) return false;

    memcpy(s_senders[slot].mac, mac, 6);
    s_senders[slot].in_use = true;
    s_senders[slot].last_seen_us = esp_timer_get_time();
    persist_senders();
    return true;
}

// Runs on the esp_timer task -- a different context from on_recv() (WiFi/
// ESP-NOW driver task), which is the only reason this needs to exist rather
// than calling esp_now_send() straight from on_recv().
static void reply_timer_cb(void *arg)
{
    espnow_pairing_msg_t reply = {0};
    memcpy(reply.magic, PAIR_MAGIC, 4);
    memcpy(reply.network_id, s_network_id, ESPNOW_PAIRING_NETID_LEN);
    reply.role = 1;
    esp_now_send(BROADCAST_MAC, (uint8_t *)&reply, sizeof(reply));
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len == sizeof(espnow_pairing_msg_t)) {
        espnow_pairing_msg_t msg;
        memcpy(&msg, data, sizeof(msg));

        if (memcmp(msg.magic, PAIR_MAGIC, 4) == 0 &&
            memcmp(msg.network_id, s_network_id, ESPNOW_PAIRING_NETID_LEN) == 0) {

            if (msg.role == 0 && s_is_receiver) {
                // A sender is looking for us. Only a receiver should ever
                // act on a hello -- without this guard, two senders
                // discovering at the same time would try to "adopt" each
                // other. add_or_evict_sender() makes room by dropping the
                // least-recently-heard-from sender if we're already full.
                if (!add_or_evict_sender(info->src_addr)) {
                    // Only fails if esp_now_add_peer() itself fails after
                    // room was made -- do NOT reply in that case. A false
                    // reply here would tell the sender it's paired when the
                    // receiver actually can't decrypt anything from it.
                    printf("[PAIRING] Could not add sender %02x:%02x:%02x:%02x:%02x:%02x "
                           "-- not replying\n",
                           info->src_addr[0], info->src_addr[1], info->src_addr[2],
                           info->src_addr[3], info->src_addr[4], info->src_addr[5]);
                } else {
                    // ESP_ERR_INVALID_STATE here just means a reply is
                    // already pending from an earlier hello in this same
                    // burst -- that's fine, one reply covers everyone.
                    esp_timer_start_once(s_reply_timer, 1000);
                    printf("[PAIRING] Registered sender %02x:%02x:%02x:%02x:%02x:%02x\n",
                           info->src_addr[0], info->src_addr[1], info->src_addr[2],
                           info->src_addr[3], info->src_addr[4], info->src_addr[5]);
                }
            } else if (msg.role == 1 && !s_is_receiver) {
                // A receiver answered our hello. Only meaningful to a
                // sender that's actively discovering; espnow_pairing_get_receiver()
                // drains any stale signal before waiting, so a late/stray
                // give here is harmless even outside that window.
                memcpy(s_learned_mac, info->src_addr, 6);
                if (s_reply_sem) xSemaphoreGive(s_reply_sem);
            }
            return;   // never forward pairing messages to the app
        }
    }

    if (s_is_receiver) touch_sender(info->src_addr);   // keeps LRU meaningful post-pairing
    if (s_app_cb) s_app_cb(info, data, len);
}

void espnow_pairing_init(const uint8_t network_id[ESPNOW_PAIRING_NETID_LEN],
                          const uint8_t lmk[16],
                          esp_now_recv_cb_t app_recv_cb)
{
    memcpy(s_network_id, network_id, ESPNOW_PAIRING_NETID_LEN);
    memcpy(s_lmk, lmk, 16);
    s_app_cb = app_recv_cb;

    if (!esp_now_is_peer_exist(BROADCAST_MAC)) {
        esp_now_peer_info_t bcast = {0};
        memcpy(bcast.peer_addr, BROADCAST_MAC, 6);
        bcast.channel = 0;
        bcast.encrypt = false;
        esp_err_t err = esp_now_add_peer(&bcast);
        if (err != ESP_OK) {
            printf("[PAIRING] Failed to add broadcast peer: %s\n", esp_err_to_name(err));
        }
        // Deliberately NOT calling set_lr_rate() here. That API negotiates
        // a PHY rate with one specific unicast peer -- applying it to the
        // broadcast address isn't a documented use case and was never
        // checked for failure. Broadcast frames use the default rate,
        // which both sides already share via esp_wifi_set_protocol(...,
        // WIFI_PROTOCOL_LR) (a device-wide setting, not per-peer). Real
        // presence-data traffic still gets LR rate via add_encrypted_peer()
        // once a peer is actually known, exactly as before pairing existed.
    }

    // Created lazily here so it exists before on_recv() can possibly touch
    // it, regardless of role.
    if (!s_reply_sem) s_reply_sem = xSemaphoreCreateBinary();

    esp_now_register_recv_cb(on_recv);
}

void espnow_pairing_receiver_init(const uint8_t network_id[ESPNOW_PAIRING_NETID_LEN],
                                   const uint8_t lmk[16],
                                   esp_now_recv_cb_t app_recv_cb)
{
    s_is_receiver = true;

    // Create the reply timer before espnow_pairing_init() registers the
    // recv callback below -- on_recv() can start firing the instant that
    // callback is live, and it calls esp_timer_start_once(s_reply_timer, ...)
    // on any matching hello. Creating the timer first guarantees s_reply_timer
    // is never NULL/uninitialized by the time anything can touch it.
    const esp_timer_create_args_t timer_args = {
        .callback = &reply_timer_cb,
        .name = "pairing_reply",
    };
    esp_timer_create(&timer_args, &s_reply_timer);

    espnow_pairing_init(network_id, lmk, app_recv_cb);

    load_known_senders();   // re-adopt previously-paired senders immediately
}

bool espnow_pairing_get_receiver(uint32_t timeout_ms, uint8_t out_mac[6])
{
    if (load_cached_peer(out_mac) && add_encrypted_peer(out_mac)) {
        return true;
    }

    // s_reply_sem is created once in espnow_pairing_init() and never
    // deleted (a create/delete-per-call cycle here would race against
    // on_recv() possibly giving it from the ESP-NOW driver task at the
    // exact moment it's being torn down). Drain any stale signal left over
    // from a previous round before waiting.
    xSemaphoreTake(s_reply_sem, 0);

    espnow_pairing_msg_t hello = {0};
    memcpy(hello.magic, PAIR_MAGIC, 4);
    memcpy(hello.network_id, s_network_id, ESPNOW_PAIRING_NETID_LEN);
    hello.role = 0;

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    bool found = false;
    while (xTaskGetTickCount() < deadline) {
        esp_now_send(BROADCAST_MAC, (uint8_t *)&hello, sizeof(hello));
        if (xSemaphoreTake(s_reply_sem, pdMS_TO_TICKS(HELLO_INTERVAL_MS)) == pdTRUE) {
            found = true;
            break;
        }
    }

    if (!found) return false;

    memcpy(out_mac, s_learned_mac, 6);
    if (!add_encrypted_peer(out_mac)) return false;
    save_cached_peer(out_mac);
    printf("[PAIRING] Paired with receiver %02x:%02x:%02x:%02x:%02x:%02x (cached)\n",
           out_mac[0], out_mac[1], out_mac[2], out_mac[3], out_mac[4], out_mac[5]);
    return true;
}

void espnow_pairing_forget_receiver(void)
{
    uint8_t mac[6];
    if (load_cached_peer(mac)) {
        esp_now_del_peer(mac);
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_PEER_KEY);
        nvs_commit(h);
        nvs_close(h);
    }
    printf("[PAIRING] Forgot cached receiver, will rediscover\n");
}

void espnow_pairing_forget_all_senders(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_SENDER_CNT_KEY);
        nvs_erase_key(h, NVS_SENDER_MACS_KEY);
        nvs_commit(h);
        nvs_close(h);
    }
    printf("[PAIRING] Forgot all paired senders, rebooting\n");
    esp_restart();   // simplest reliable way to also clear the in-RAM peer table
}
