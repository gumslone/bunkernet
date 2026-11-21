// BunkerNet - offline mesh network for ESP8266 / ESP-01
//
// Backbone : ESP-NOW broadcast flooding (TTL + replay window), encrypted with ChaCha20-Poly1305
// Clients  : each node is a Wi-Fi AP (same SSID everywhere) + captive portal web app
// Features : group chat, direct messages with delivery receipts and store-and-forward,
//            shared data board, emergency alerts, node map. Same firmware on every node.
// Build with -DRELAY_ONLY=1 for a node that only relays (no access point, no web server).

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <espnow.h>
#include <LittleFS.h>
#include <bearssl/bearssl.h>
extern "C" {
#include <user_interface.h>
}
#include "config.h"
#include "mesh_logic.h"
#include "web_ui.h"

#ifndef RELAY_ONLY
#define RELAY_ONLY 0
#endif

#if !RELAY_ONLY && defined(OTA_PASSWORD)
#include <ESP8266HTTPUpdateServer.h>
#endif

ADC_MODE(ADC_VCC);   // lets ESP.getVcc() report the supply voltage (A0 is not used)

// ---------------------------------------------------------------- constants
#define MAGIC        0xA7
#define OFF_TTL      1
#define OFF_NONCE    2          // origin(4) boot(4) seq(4)
#define OFF_LEN      14
#define OFF_PAYLOAD  15
#define TAG_LEN      16
#define PKT_MAX      250
#define PT_MAX       (PKT_MAX - OFF_PAYLOAD - TAG_LEN)   // 219
#define PT_HDR       19         // type ittl from to mid ts nlen
#define NAME_MAX_LEN BN_NAME_MAX
#define TEXT_MAX_LEN BN_TEXT_MAX

enum : uint8_t { T_CHAT = 1, T_DM = 2, T_PRESENCE = 3, T_BEACON = 4, T_ACK = 5, T_DATA = 6, T_ALERT = 7, T_ROOM = 8 };
#define F_PRIO 0x80   // flag bit on the message type: priority message

#define RXQ_N   8
#define TXQ_N   16
#define REPLAY_N 24
#define AUTH_N  24
#define STATE_SAVE_MS 60000UL
#define PBKDF2_ITERS  4096
#define LOG_N   28
#define USERS_N 20
#define NODES_N 16
#define DATA_N  16
#define ALERT_N 4
#define MAIL_N  12

// ---------------------------------------------------------------- state
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint32_t myId, bootCount = 1, seqNo = 0;
static bool usingDefaultKey = false;
static uint8_t  meshKey[32];
static char     nodeName[NAME_MAX_LEN + 1];

#if !RELAY_ONLY
ESP8266WebServer server(80);
DNSServer        dns;
#endif

// receive queue (filled from the ESP-NOW callback, drained in loop())
struct RxPkt { uint8_t len; uint8_t d[PKT_MAX]; };
static RxPkt rxq[RXQ_N];
static volatile uint8_t rxHead = 0, rxTail = 0;

// transmit queue with per-packet send time (jitter avoids collisions)
struct TxPkt { bool used, damp; uint8_t len, dupes; uint32_t at, key; uint8_t d[PKT_MAX]; };
static TxPkt txq[TXQ_N];
static uint32_t lastTx = 0;

static bn::ReplayTable<REPLAY_N> replay;
static bn::AuthTable<AUTH_N> authTab;
static bn::Mailbox<MAIL_N> mailbox;
static bn::MeshClock meshClock;
static bn::RateLimiter<8> limiter;
static bool stateDirty = false;
static uint32_t lastSave = 0;
using bn::Mail;

// message log shown to local clients
struct LogEnt {
  uint32_t lid; uint8_t type; bool acked, prio;
  uint32_t from, to, mid, ts;
  char name[NAME_MAX_LEN + 1];
  char text[TEXT_MAX_LEN + 1];
};
static LogEnt  logRing[LOG_N];
static uint32_t logCount = 0;

struct User {
  uint32_t id; uint32_t node; uint32_t seen; bool needPresence;
  char name[NAME_MAX_LEN + 1];
};
static User users[USERS_N];

struct NodeEnt { uint32_t id; uint32_t seen; uint16_t vcc; uint8_t hops; char name[NAME_MAX_LEN + 1]; };
static NodeEnt nodes[NODES_N];

struct DataEnt { bool used; uint32_t from, ts; char k[NAME_MAX_LEN + 1]; char v[48]; };
static DataEnt board[DATA_N];
static uint32_t dataVer = 1;

struct AlertEnt { bool used; uint32_t mid, from, ts; char name[NAME_MAX_LEN + 1]; char text[TEXT_MAX_LEN + 1]; };
static AlertEnt alerts[ALERT_N];
static uint32_t alertVer = 1;

// alerts we originated are re-flooded for a while so late/offline nodes still get them
struct AlertRep { bool active; uint8_t left; uint32_t next, mid, from; char name[NAME_MAX_LEN + 1]; char text[TEXT_MAX_LEN + 1]; };
static AlertRep rep;

static uint32_t tBeacon = 0, tPresence = 0, tRebase = 0;

#if !RELAY_ONLY && defined(OTA_PASSWORD)
static ESP8266HTTPUpdateServer httpUpdater;
#endif

#define MLOG(...) do { if (DEBUG_SERIAL) Serial.printf(__VA_ARGS__); } while (0)

// ---------------------------------------------------------------- helpers
static void put32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t get32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint32_t rnd32() { return RANDOM_REG32; }
// supply voltage of the module in millivolts (the SDK reports 1/1024 V)
static uint16_t vccMv() { return (uint16_t)((uint32_t)ESP.getVcc() * 1000UL / 1024UL); }

static uint32_t fnv(const uint8_t* p, size_t n) {
  uint32_t h = 2166136261UL;
  while (n--) { h ^= *p++; h *= 16777619UL; }
  return h;
}

// copy at most max bytes without cutting a UTF-8 character in half
static void copyUtf8(char* dst, const char* src, size_t srcLen, size_t max) {
  size_t n = bn::utf8_fit(src, srcLen, max);
  memcpy(dst, src, n);
  dst[n] = 0;
}

static void led(bool on) {
#if LED_PIN >= 0
  digitalWrite(LED_PIN, on ? LOW : HIGH);
#endif
}

// ---------------------------------------------------------------- tx queue
static void txEnqueue(const uint8_t* d, uint8_t len, uint32_t at, uint32_t key = 0, bool damp = false) {
  for (uint8_t i = 0; i < TXQ_N; i++) {
    if (!txq[i].used) {
      txq[i].used = true; txq[i].len = len; txq[i].at = at;
      txq[i].key = key; txq[i].damp = damp; txq[i].dupes = 0;
      memcpy(txq[i].d, d, len);
      return;
    }
  }
  MLOG("tx queue full\n");
}

static void txPump() {
  uint32_t now = millis();
  if (now - lastTx < 6) return;
  for (uint8_t i = 0; i < TXQ_N; i++) {
    if (txq[i].used && (int32_t)(now - txq[i].at) >= 0) {
      txq[i].used = false;
      // periodic traffic (beacons, presence) is skipped when neighbours already relayed it
      if (txq[i].damp && bn::relay_damped(txq[i].dupes, rnd32())) continue;
      esp_now_send((uint8_t*)BCAST, txq[i].d, txq[i].len);
      lastTx = now; led(true);
      return;
    }
  }
}

// a copy of a packet we are about to relay was heard from another node
static void noteDuplicate(uint32_t key) {
  for (uint8_t i = 0; i < TXQ_N; i++)
    if (txq[i].used && txq[i].damp && txq[i].key == key && txq[i].dupes < 255) txq[i].dupes++;
}

// ---------------------------------------------------------------- mesh send
static uint32_t meshSend(uint8_t type, uint32_t from, uint32_t to, uint32_t mid,
                         const char* name, const char* text, uint32_t ts = 0, uint8_t ttl = MESH_TTL,
                         bool prio = false) {
  uint8_t pt[PT_MAX];
  size_t nl = strlen(name); if (nl > NAME_MAX_LEN) nl = NAME_MAX_LEN;
  size_t tl = strlen(text); if (tl > TEXT_MAX_LEN) tl = TEXT_MAX_LEN;
  if (mid == 0) mid = rnd32() | 1;
  pt[0] = type | (prio ? F_PRIO : 0); pt[1] = ttl;
  put32(pt + 2, from); put32(pt + 6, to); put32(pt + 10, mid); put32(pt + 14, ts);
  pt[18] = (uint8_t)nl;
  memcpy(pt + PT_HDR, name, nl);
  memcpy(pt + PT_HDR + nl, text, tl);
  size_t n = PT_HDR + nl + tl;

  uint8_t pkt[PKT_MAX];
  pkt[0] = MAGIC; pkt[OFF_TTL] = ttl;
  seqNo++;
  put32(pkt + OFF_NONCE, myId); put32(pkt + OFF_NONCE + 4, bootCount); put32(pkt + OFF_NONCE + 8, seqNo);
  pkt[OFF_LEN] = (uint8_t)n;
  memcpy(pkt + OFF_PAYLOAD, pt, n);
  uint8_t tag[TAG_LEN];
  br_poly1305_ctmul_run(meshKey, pkt + OFF_NONCE, pkt + OFF_PAYLOAD, n,
                        pkt + OFF_NONCE, 12, tag, br_chacha20_ct_run, 1);
  memcpy(pkt + OFF_PAYLOAD + n, tag, TAG_LEN);

  txEnqueue(pkt, OFF_PAYLOAD + n + TAG_LEN, millis() + 2 + rnd32() % 12);
  return mid;
}

// ---------------------------------------------------------------- tables
static User* findUser(uint32_t id) {
  for (auto& u : users) if (u.id == id) return &u;
  return nullptr;
}

static User* touchUser(uint32_t id, const char* name, uint32_t node) {
  uint32_t now = millis();
  User* u = findUser(id);
  if (!u) {
    User* oldest = &users[0];
    for (auto& x : users) {
      if (x.id == 0) { oldest = &x; break; }
      if (now - x.seen > now - oldest->seen) oldest = &x;
    }
    u = oldest;
    memset(u, 0, sizeof(*u));
    u->id = id; u->needPresence = (node == myId);
  }
  char clean[NAME_MAX_LEN + 1];                       // compare the stored (UTF-8 safe) form
  copyUtf8(clean, name, strlen(name), NAME_MAX_LEN);
  if (node == myId && strcmp(u->name, clean) != 0) u->needPresence = true;
  memcpy(u->name, clean, sizeof(clean));
  u->node = node; u->seen = now;
  return u;
}

static bool userIsLocal(uint32_t id) {
  User* u = findUser(id);
  return u && u->node == myId && millis() - u->seen < 120000UL;
}

static void touchNode(uint32_t id, const char* name, uint8_t hops, uint16_t vcc) {
  uint32_t now = millis();
  NodeEnt* e = nullptr;
  for (auto& n : nodes) if (n.id == id) { e = &n; break; }
  if (!e) {
    e = &nodes[0];
    for (auto& n : nodes) {
      if (n.id == 0) { e = &n; break; }
      if (now - n.seen > now - e->seen) e = &n;
    }
    memset(e, 0, sizeof(*e)); e->id = id;
  }
  copyUtf8(e->name, name, strlen(name), NAME_MAX_LEN);
  e->hops = hops; e->seen = now; e->vcc = vcc;
}

static void setData(uint32_t from, const char* k, const char* v, uint32_t ts) {
  DataEnt* e = nullptr;
  for (auto& d : board) if (d.used && strcmp(d.k, k) == 0) { e = &d; break; }
  if (!e) for (auto& d : board) if (!d.used) { e = &d; break; }
  if (!e) e = &board[rnd32() % DATA_N];          // board full: replace a random entry
  e->used = true; e->from = from; e->ts = ts;
  copyUtf8(e->k, k, strlen(k), NAME_MAX_LEN);
  copyUtf8(e->v, v, strlen(v), sizeof(e->v) - 1);   // never cut a UTF-8 character in half
  dataVer++;
}

static LogEnt* addLog(uint8_t type, uint32_t from, uint32_t to, uint32_t mid,
                      const char* name, const char* text, uint32_t ts = 0, bool prio = false) {
  LogEnt& e = logRing[logCount % LOG_N];
  logCount++;
  e.lid = logCount; e.type = type; e.acked = false; e.prio = prio;
  e.from = from; e.to = to; e.mid = mid; e.ts = ts;
  strncpy(e.name, name, NAME_MAX_LEN); e.name[NAME_MAX_LEN] = 0;
  strncpy(e.text, text, TEXT_MAX_LEN); e.text[TEXT_MAX_LEN] = 0;
  return &e;
}

static Mail* findMail(uint32_t mid) { return mailbox.find(mid); }

static void markDelivered(uint32_t mid) {
  if (mailbox.markDelivered(mid)) stateDirty = true;
}

static Mail* storeMail(uint32_t from, uint32_t to, uint32_t mid, uint32_t ts,
                       const char* name, const char* text, bool mine, bool prio = false) {
  stateDirty = true;
  return mailbox.store(from, to, mid, ts, name, text, mine, millis(), prio);
}

// alert with empty text = cancel of the alert with that mid
static void setAlert(uint32_t mid, uint32_t from, const char* name, const char* text) {
  bool cancel = !text[0];
  if (cancel && rep.active && rep.mid == mid) rep.active = false;
  for (auto& a : alerts) {
    if (a.used && a.mid == mid) {
      if (cancel) { a.used = false; alertVer++; }
      return;                                   // duplicate repeat: ignore
    }
  }
  if (cancel) return;
  AlertEnt* e = &alerts[0];
  for (auto& a : alerts) {
    if (!a.used) { e = &a; break; }
    if (millis() - a.ts > millis() - e->ts) e = &a;
  }
  e->used = true; e->mid = mid; e->from = from; e->ts = millis();
  strncpy(e->name, name, NAME_MAX_LEN); e->name[NAME_MAX_LEN] = 0;
  strncpy(e->text, text, TEXT_MAX_LEN); e->text[TEXT_MAX_LEN] = 0;
  alertVer++;
}

static void raiseAlert(uint32_t from, const char* name, const char* text) {
  uint32_t mid = meshSend(T_ALERT, from, 0, 0, name, text);
  setAlert(mid, from, name, text);
  rep.active = true; rep.left = 10; rep.next = millis() + 30000UL;
  rep.mid = mid; rep.from = from;
  strncpy(rep.name, name, NAME_MAX_LEN); rep.name[NAME_MAX_LEN] = 0;
  strncpy(rep.text, text, TEXT_MAX_LEN); rep.text[TEXT_MAX_LEN] = 0;
}

// delivery receipt: tell the sender's node that `from` (recipient) got message `mid`
static void sendAck(uint32_t recipient, uint32_t sender, uint32_t mid) {
  markDelivered(mid);
  if (userIsLocal(sender)) addLog(T_ACK, recipient, sender, mid, "", "");
  meshSend(T_ACK, recipient, sender, mid, "", "");
}

// ---------------------------------------------------------------- key derivation, identity
// PBKDF2-HMAC-SHA256: makes guessing a weak passphrase from a captured packet slow.
static void deriveKey(const char* pw, uint8_t out[32]) {
  static const uint8_t salt[] = "BunkerNet-mesh-key-v1";
  br_hmac_key_context kc;
  br_hmac_context hc;
  br_hmac_key_init(&kc, &br_sha256_vtable, pw, strlen(pw));
  uint8_t u[32], t[32];
  static const uint8_t one[4] = {0, 0, 0, 1};
  br_hmac_init(&hc, &kc, 0);
  br_hmac_update(&hc, salt, sizeof(salt) - 1);
  br_hmac_update(&hc, one, 4);
  br_hmac_out(&hc, u);
  memcpy(t, u, 32);
  for (uint32_t i = 1; i < PBKDF2_ITERS; i++) {
    br_hmac_init(&hc, &kc, 0);
    br_hmac_update(&hc, u, 32);
    br_hmac_out(&hc, u);
    for (uint8_t j = 0; j < 32; j++) t[j] ^= u[j];
    if ((i & 255) == 0) yield();
  }
  memcpy(out, t, 32);
}

// A client proves it owns an id by sending its secret token; we only keep a short hash of it.
static void tokenHash(const char* tok, uint8_t out[8]) {
  br_sha256_context c; uint8_t d[32];
  br_sha256_init(&c);
  br_sha256_update(&c, "bunkernet-token:", 16);
  br_sha256_update(&c, tok, strlen(tok));
  br_sha256_out(&c, d);
  memcpy(out, d, 8);
}

static void hashToHex(const uint8_t h[8], char out[17]) {
  for (uint8_t i = 0; i < 8; i++) sprintf(out + i * 2, "%02x", h[i]);
}
static bool hexToHash(const char* s, uint8_t out[8]) {
  if (strlen(s) != 16) return false;
  for (uint8_t i = 0; i < 8; i++) {
    char b[3] = {s[i * 2], s[i * 2 + 1], 0}; char* e;
    out[i] = (uint8_t)strtoul(b, &e, 16);
    if (*e) return false;
  }
  return true;
}

// ---------------------------------------------------------------- persistence (LittleFS)
// Identity claims, stored private messages and the boot counter survive power loss.
#define STATE_MAGIC 0x424E5333UL
struct StateHdr { uint32_t magic, size, boot; };
static const size_t STATE_SIZE = sizeof(StateHdr) + sizeof(bn::AuthEnt) * AUTH_N + sizeof(bn::Mail) * MAIL_N;

static void saveState() {
  File f = LittleFS.open("/state.tmp", "w");
  if (!f) return;
  StateHdr h = {STATE_MAGIC, (uint32_t)STATE_SIZE, bootCount};
  bool ok = f.write((uint8_t*)&h, sizeof(h)) == sizeof(h);
  ok = ok && f.write((uint8_t*)authTab.raw(), sizeof(bn::AuthEnt) * AUTH_N) == sizeof(bn::AuthEnt) * AUTH_N;
  ok = ok && f.write((uint8_t*)mailbox.raw(), sizeof(bn::Mail) * MAIL_N) == sizeof(bn::Mail) * MAIL_N;
  f.close();
  if (ok && LittleFS.rename("/state.tmp", "/state.bin")) { stateDirty = false; lastSave = millis(); }
  else LittleFS.remove("/state.tmp");
}

static void loadState() {
  bool loaded = false;
  if (LittleFS.begin()) {
    File f = LittleFS.open("/state.bin", "r");
    if (f) {
      StateHdr h;
      if (f.size() == STATE_SIZE && f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) &&
          h.magic == STATE_MAGIC && h.size == STATE_SIZE) {
        f.read((uint8_t*)authTab.raw(), sizeof(bn::AuthEnt) * AUTH_N);
        f.read((uint8_t*)mailbox.raw(), sizeof(bn::Mail) * MAIL_N);
        bootCount = h.boot + 1;
        loaded = true;
      }
      f.close();
    }
  } else {
    MLOG("LittleFS mount failed, running without persistence\n");
  }
  if (!loaded) {
    // No usable state (first boot, erased flash): start the counter high and random so peers,
    // which remember the old counter, still accept this node as "rebooted", not "replayed".
    bootCount = 1000 + rnd32() % 60000;
    memset(authTab.raw(), 0, sizeof(bn::AuthEnt) * AUTH_N);
    memset(mailbox.raw(), 0, sizeof(bn::Mail) * MAIL_N);
  }
  bn::Mail* m = mailbox.raw();
  for (size_t i = 0; i < MAIL_N; i++) {              // re-arm retry timers relative to this boot
    if (!m[i].used) continue;
    uint8_t t = m[i].tries < bn::RETRY_N ? m[i].tries : bn::RETRY_N - 1;
    m[i].next = millis() + bn::RETRY_S[t] * 1000UL;
    m[i].lastAck = 0;
  }
  saveState();                                       // persist the new boot counter right away
}

// ---------------------------------------------------------------- rx handling
static void handlePlain(const uint8_t* pt, size_t n, uint8_t ttl, uint32_t origin) {
  if (n < PT_HDR) return;
  uint8_t type = pt[0] & 0x7F, ittl = pt[1];
  bool prio = (pt[0] & F_PRIO) != 0;
  uint32_t from = get32(pt + 2), to = get32(pt + 6), mid = get32(pt + 10), ts = get32(pt + 14);
  size_t nl = pt[18];
  if (PT_HDR + nl > n) return;
  char name[NAME_MAX_LEN + 1], text[TEXT_MAX_LEN + 1];
  copyUtf8(name, (const char*)pt + PT_HDR, nl, NAME_MAX_LEN);
  size_t tl = n - PT_HDR - nl;
  copyUtf8(text, (const char*)pt + PT_HDR + nl, tl, TEXT_MAX_LEN);
  uint8_t hops = (ittl >= ttl) ? (ittl - ttl + 1) : 1;

  switch (type) {
    case T_CHAT:
    case T_ROOM:
      addLog(type, from, to, mid, name, text, ts, prio);
      break;
    case T_DM: {
      Mail* mm = findMail(mid);
      if (mm) {                                   // re-flooded copy of a message we already hold
        if (mm->delivered && millis() - mm->lastAck > 20000UL) {
          mm->lastAck = millis();
          sendAck(to, from, mid);                 // tell the origin to stop retrying
        }
        break;
      }
      storeMail(from, to, mid, ts, name, text, false, prio);
      addLog(T_DM, from, to, mid, name, text, ts, prio);
      break;
    }
    case T_PRESENCE: {
      uint8_t th[8];
      if (hexToHash(text, th)) {                  // presence carries the identity claim
        bn::AuthTable<AUTH_N>::Result r = authTab.check(from, th);
        if (r == bn::AuthTable<AUTH_N>::BAD) break;      // id already owned by someone else
        if (r == bn::AuthTable<AUTH_N>::NEW) { authTab.learn(from, th); stateDirty = true; }
      }
      touchUser(from, name, origin);
      break;
    }
    case T_BEACON: {
      // beacon text is "<unix time or 0>;<supply mV>"
      char* semi = nullptr;
      uint32_t t = (uint32_t)strtoul(text, &semi, 10);
      uint16_t v = (semi && *semi == ';') ? (uint16_t)strtoul(semi + 1, nullptr, 10) : 0;
      if (t) meshClock.feed(from, t, millis());
      touchNode(from, name, hops, v);
      break;
    }
    case T_ACK:
      markDelivered(mid);
      if (userIsLocal(to)) addLog(T_ACK, from, to, mid, "", "");
      break;
    case T_DATA:
      setData(from, name, text, ts);
      break;
    case T_ALERT:
      setAlert(mid, from, name, text);
      break;
  }
}

static void processRx() {
  while (rxTail != rxHead) {
    RxPkt& r = rxq[rxTail];
    uint8_t* d = r.d; uint8_t len = r.len;
    if (len >= OFF_PAYLOAD + TAG_LEN && d[0] == MAGIC) {
      uint8_t n = d[OFF_LEN];
      uint32_t origin = get32(d + OFF_NONCE), boot = get32(d + OFF_NONCE + 4), seq = get32(d + OFF_NONCE + 8);
      uint32_t now = millis();
      if (len == OFF_PAYLOAD + n + TAG_LEN && n >= PT_HDR && origin != myId) {
        uint32_t key = fnv(d + OFF_NONCE, 12);
        if (!replay.check(origin, boot, seq, now)) {
          noteDuplicate(key);                           // duplicate or replayed packet
        } else {
          uint8_t buf[PT_MAX], tag[TAG_LEN];
          memcpy(buf, d + OFF_PAYLOAD, n);
          br_poly1305_ctmul_run(meshKey, d + OFF_NONCE, buf, n, d + OFF_NONCE, 12,
                                tag, br_chacha20_ct_run, 0);
          uint8_t diff = 0;
          for (uint8_t i = 0; i < TAG_LEN; i++) diff |= tag[i] ^ d[OFF_PAYLOAD + n + i];
          if (diff == 0) {
            replay.commit(origin, boot, seq, now);
            uint8_t ttl = d[OFF_TTL];
            if (ttl > 1) {                              // relay with random delay
              d[OFF_TTL] = ttl - 1;
              bool damp = (buf[0] & 0x7F) == T_BEACON || (buf[0] & 0x7F) == T_PRESENCE;
              txEnqueue(d, len, now + 10 + rnd32() % 70, key, damp);
            }
            handlePlain(buf, n, ttl, origin);
            led(true);
          }
        }
      }
    }
    rxTail = (rxTail + 1) % RXQ_N;
  }
}

// ESP-NOW callback: keep it tiny, just queue the frame
static void onRecv(uint8_t* mac, uint8_t* data, uint8_t len) {
  (void)mac;
  if (len > PKT_MAX || len < OFF_PAYLOAD + TAG_LEN) return;
  uint8_t nx = (rxHead + 1) % RXQ_N;
  if (nx == rxTail) return;
  memcpy(rxq[rxHead].d, data, len);
  rxq[rxHead].len = len;
  rxHead = nx;
}

#if !RELAY_ONLY
// ---------------------------------------------------------------- web helpers
static void hex8(char* out, uint32_t v) { sprintf(out, "%08x", (unsigned)v); }
static uint32_t argHex(const char* k) {
  return server.hasArg(k) ? (uint32_t)strtoul(server.arg(k).c_str(), nullptr, 16) : 0;
}

static void jesc(String& o, const char* s) {
  o += '"';
  for (; *s; s++) {
    uint8_t c = *s;
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c < 0x20) { char b[8]; sprintf(b, "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  o += '"';
}

static void beginJson() {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
}
static void flushJson(String& o, bool force = false) {
  if (force || o.length() > 900) { server.sendContent(o); o = ""; }
}

// Every client request must prove it owns `me` with its secret token (`tok`).
// The first claim for an id wins (trust on first use) and is flooded to the other nodes.
static bool authClient(uint32_t me) {
  String tok = server.arg("tok");
  if (!me || tok.length() < 16 || tok.length() > 64) { server.send(403, "text/plain", "auth"); return false; }
  uint8_t h[8];
  tokenHash(tok.c_str(), h);
  bn::AuthTable<AUTH_N>::Result r = authTab.check(me, h);
  if (r == bn::AuthTable<AUTH_N>::BAD) { server.send(403, "text/plain", "auth"); return false; }
  if (r == bn::AuthTable<AUTH_N>::NEW) { authTab.learn(me, h); stateDirty = true; }
  if (server.hasArg("ts")) meshClock.feed(me, (uint32_t)strtoul(server.arg("ts").c_str(), nullptr, 10), millis());
  String nm = server.arg("name");
  User* u = findUser(me);
  if (u && !nm.length()) { u->node = myId; u->seen = millis(); }   // request without a name keeps the old one
  else u = touchUser(me, nm.length() ? nm.c_str() : "?", myId);
  if (r == bn::AuthTable<AUTH_N>::NEW) u->needPresence = true;
  return true;
}

// Writes that go out on the mesh are limited per client and per node, so one phone cannot flood everyone.
static bool rateOk(uint32_t me) {
  uint32_t now = millis();
  if (limiter.allow(me, now, 12, 10000UL) && limiter.allow(0, now, 60, 10000UL)) return true;
  server.send(429, "text/plain", "slow down");
  return false;
}

static bool lowHeap() {
  if (ESP.getFreeHeap() >= 5000 && ESP.getMaxFreeBlockSize() >= 2500) return false;
  server.send(503, "text/plain", "busy");
  return true;
}

static void handlePoll() {
  uint32_t me = argHex("me");
  if (lowHeap() || !authClient(me)) return;
  uint32_t since = server.arg("since").toInt();
  uint32_t cdv = server.arg("dv").toInt();
  uint32_t cav = server.arg("av").toInt();

  // issue delivery receipts for DMs addressed to this client
  for (auto& e : logRing) {
    if (e.lid && e.type == T_DM && e.to == me && !e.acked && e.from != me) {
      e.acked = true;
      sendAck(me, e.from, e.mid);
    }
  }

  beginJson();
  String o; o.reserve(1100);
  char h[9];
  o += "{\"node\":\""; hex8(h, myId); o += (h + 4); o += "\",\"last\":"; o += logCount;
  o += ",\"mt\":"; o += meshClock.now(millis());
  o += ",\"msgs\":[";
  uint32_t first = logCount > LOG_N ? logCount - LOG_N + 1 : 1;
  if (since + 1 > first) first = since + 1;
  bool comma = false;
  for (uint32_t lid = first; lid <= logCount; lid++) {
    LogEnt& e = logRing[(lid - 1) % LOG_N];
    bool vis = e.type == T_CHAT || e.type == T_ROOM || (e.type == T_DM && (e.from == me || e.to == me)) ||
               (e.type == T_ACK && e.to == me);
    if (!vis) continue;
    if (comma) o += ',';
    comma = true;
    o += "{\"t\":"; o += e.type;
    hex8(h, e.from); o += ",\"f\":\""; o += h;
    hex8(h, e.to);   o += "\",\"to\":\""; o += h;
    hex8(h, e.mid);  o += "\",\"m\":\""; o += h;
    o += "\",\"s\":"; o += e.ts;
    if (e.prio) o += ",\"p\":1";
    o += ",\"n\":"; jesc(o, e.name);
    o += ",\"x\":"; jesc(o, e.text);
    o += '}';
    flushJson(o);
  }
  // store-and-forward: DMs that were held for this user while they were away
  bn::Mail* mails = mailbox.raw();
  for (size_t mi = 0; mi < MAIL_N; mi++) {
    bn::Mail& mm = mails[mi];
    if (!mm.used || mm.delivered || mm.to != me) continue;
    sendAck(me, mm.from, mm.mid);
    if (comma) o += ',';
    comma = true;
    o += "{\"t\":2";
    hex8(h, mm.from); o += ",\"f\":\""; o += h;
    hex8(h, mm.to);   o += "\",\"to\":\""; o += h;
    hex8(h, mm.mid);  o += "\",\"m\":\""; o += h;
    o += "\",\"s\":"; o += mm.ts;
    if (mm.prio) o += ",\"p\":1";
    o += ",\"n\":"; jesc(o, mm.name);
    o += ",\"x\":"; jesc(o, mm.text);
    o += '}';
    flushJson(o);
  }
  o += "],\"users\":[";
  comma = false;
  uint32_t now = millis();
  for (auto& u : users) {
    if (!u.id || now - u.seen > USER_TTL_MS) continue;
    if (comma) o += ',';
    comma = true;
    hex8(h, u.id); o += "{\"id\":\""; o += h; o += "\",\"n\":"; jesc(o, u.name); o += '}';
  }
  o += "],\"dv\":"; o += dataVer;
  if (cdv != dataVer) {
    o += ",\"data\":[";
    comma = false;
    for (auto& d : board) {
      if (!d.used) continue;
      if (comma) o += ',';
      comma = true;
      hex8(h, d.from);
      o += "{\"k\":"; jesc(o, d.k); o += ",\"v\":"; jesc(o, d.v);
      o += ",\"s\":"; o += d.ts;
      o += ",\"f\":\""; o += h; o += "\"}";
      flushJson(o);
    }
    o += ']';
  }
  o += ",\"av\":"; o += alertVer;
  if (cav != alertVer) {
    o += ",\"alerts\":[";
    comma = false;
    for (auto& a : alerts) {
      if (!a.used) continue;
      if (comma) o += ',';
      comma = true;
      hex8(h, a.mid);
      o += "{\"m\":\""; o += h; o += "\",\"n\":"; jesc(o, a.name);
      o += ",\"x\":"; jesc(o, a.text); o += '}';
      flushJson(o);
    }
    o += ']';
  }
  o += '}';
  flushJson(o, true);
  server.sendContent("");
}

static void handleSend() {
  uint32_t me = argHex("me"), to = argHex("to");
  if (lowHeap() || !authClient(me) || !rateOk(me)) return;
  if (!server.hasArg("x")) { server.send(400, "text/plain", "bad"); return; }
  String nm = server.arg("name"), x = server.arg("x");
  char name[NAME_MAX_LEN + 1], text[TEXT_MAX_LEN + 1];
  copyUtf8(name, nm.c_str(), nm.length(), NAME_MAX_LEN);
  copyUtf8(text, x.c_str(), x.length(), TEXT_MAX_LEN);
  if (!text[0]) { server.send(400, "text/plain", "empty"); return; }
  uint32_t room = argHex("room");
  uint8_t type = room ? T_ROOM : (to ? T_DM : T_CHAT);
  if (room) to = room;
  bool prio = server.arg("p") == "1";
  uint32_t ts = meshClock.stamp((uint32_t)strtoul(server.arg("ts").c_str(), nullptr, 10), millis());
  uint32_t mid = meshSend(type, me, to, 0, name, text, ts, MESH_TTL, prio);
  addLog(type, me, to, mid, name, text, ts, prio);
  if (type == T_DM) storeMail(me, to, mid, ts, name, text, true, prio);
  server.send(200, "text/plain", "ok");
}

static void handlePut() {
  uint32_t me = argHex("me");
  if (lowHeap() || !authClient(me) || !rateOk(me)) return;
  if (!server.hasArg("k")) { server.send(400, "text/plain", "bad"); return; }
  String nm = server.arg("name"), k = server.arg("k"), v = server.arg("v");
  char name[NAME_MAX_LEN + 1], key[NAME_MAX_LEN + 1], val[48];
  copyUtf8(name, nm.c_str(), nm.length(), NAME_MAX_LEN);
  copyUtf8(key, k.c_str(), k.length(), NAME_MAX_LEN);
  copyUtf8(val, v.c_str(), v.length(), 47);
  if (!key[0]) { server.send(400, "text/plain", "bad"); return; }
  uint32_t ts = meshClock.stamp((uint32_t)strtoul(server.arg("ts").c_str(), nullptr, 10), millis());
  setData(me, key, val, ts);
  meshSend(T_DATA, me, 0, 0, key, val, ts);
  server.send(200, "text/plain", "ok");
}

static void handleAlert() {
  uint32_t me = argHex("me");
  if (lowHeap() || !authClient(me) || !rateOk(me)) return;
  if (!server.hasArg("x")) { server.send(400, "text/plain", "bad"); return; }
  String nm = server.arg("name"), x = server.arg("x");
  char name[NAME_MAX_LEN + 1], text[TEXT_MAX_LEN + 1];
  copyUtf8(name, nm.c_str(), nm.length(), NAME_MAX_LEN);
  copyUtf8(text, x.c_str(), x.length(), TEXT_MAX_LEN);
  if (!text[0]) { server.send(400, "text/plain", "empty"); return; }
  raiseAlert(me, name, text);
  server.send(200, "text/plain", "ok");
}

static void handleAlertClear() {
  uint32_t me = argHex("me"), mid = argHex("m");
  if (!authClient(me) || !rateOk(me)) return;
  if (!mid) { server.send(400, "text/plain", "bad"); return; }
  setAlert(mid, me, "", "");
  meshSend(T_ALERT, me, 0, mid, "", "");
  server.send(200, "text/plain", "ok");
}

static void handleNodes() {
  beginJson();
  String o; o.reserve(600);
  char h[9]; hex8(h, myId);
  o += "{\"id\":\""; o += (h + 4); o += "\",\"up\":"; o += millis() / 1000;
  o += ",\"heap\":"; o += ESP.getFreeHeap();
  o += ",\"clients\":"; o += WiFi.softAPgetStationNum();
  o += ",\"defkey\":"; o += usingDefaultKey ? 1 : 0;
  o += ",\"mt\":"; o += meshClock.now(millis());
  o += ",\"nodes\":[{\"id\":\""; o += (h + 4); o += "\",\"n\":"; jesc(o, nodeName); o += ",\"h\":0,\"a\":0,\"v\":"; o += vccMv(); o += "}";
  uint32_t now = millis();
  for (auto& n : nodes) {
    if (!n.id || now - n.seen > NODE_TTL_MS) continue;
    hex8(h, n.id);
    o += ",{\"id\":\""; o += (h + 4); o += "\",\"n\":"; jesc(o, n.name);
    o += ",\"h\":"; o += n.hops; o += ",\"a\":"; o += (now - n.seen) / 1000;
    o += ",\"v\":"; o += n.vcc; o += '}';
    flushJson(o);
  }
  o += "]}";
  flushJson(o, true);
  server.sendContent("");
}

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", PAGE_HTML);
}

// captive portal: anything unknown (connectivity probes etc.) goes to the app
static void handleNotFound() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

#endif  // !RELAY_ONLY

// ---------------------------------------------------------------- setup / loop
void setup() {
#if DEBUG_SERIAL
  Serial.begin(115200);
  Serial.println();
#endif
#if LED_PIN >= 0
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
#endif
#if SOS_PIN >= 0
  pinMode(SOS_PIN, INPUT_PULLUP);
#endif
  myId = ESP.getChipId();
  snprintf(nodeName, sizeof(nodeName), "Node-%04X", (unsigned)(myId & 0xFFFF));

  usingDefaultKey = strcmp(MESH_PSK, "change-this-to-a-long-secret-phrase") == 0;
  if (usingDefaultKey) MLOG("WARNING: default mesh key in use. Run tools/new-psk.sh\n");
  else if (strlen(MESH_PSK) < 16) MLOG("WARNING: mesh key is short, use 16+ characters\n");
  deriveKey(MESH_PSK, meshKey);

#if MESH_PHY_11B
  wifi_set_phy_mode(PHY_MODE_11B);          // experimental: slower but more sensitive radio mode
#endif
  WiFi.persistent(false);
  WiFi.setAutoConnect(false);
#if RELAY_ONLY
  WiFi.mode(WIFI_STA);                       // relay-only: no access point, no web server
  WiFi.disconnect();
  wifi_set_channel(MESH_CHANNEL);
#else
  WiFi.mode(WIFI_AP_STA);
  IPAddress ip(192, 168, 4, 1);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(MESH_SSID, AP_PASSWORD[0] ? AP_PASSWORD : nullptr, MESH_CHANNEL, 0, MAX_CLIENTS);
  WiFi.disconnect();
#endif

  WiFi.setOutputPower(TX_POWER_DBM);
  loadState();                               // after the radio is on: the hardware RNG needs it
  if (esp_now_init() != 0) { MLOG("esp_now_init failed\n"); delay(1000); ESP.restart(); }
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_add_peer((uint8_t*)BCAST, ESP_NOW_ROLE_COMBO, MESH_CHANNEL, NULL, 0);
  esp_now_register_recv_cb(onRecv);

#if !RELAY_ONLY
  dns.start(53, "*", ip);
  server.on("/", handleRoot);
  server.on("/poll", handlePoll);
  server.on("/send", handleSend);
  server.on("/put", handlePut);
  server.on("/nodes", handleNodes);
  server.on("/alert", handleAlert);
  server.on("/alertclear", handleAlertClear);
  server.onNotFound(handleNotFound);
#if defined(OTA_PASSWORD)
  httpUpdater.setup(&server, "/update", OTA_USER, OTA_PASSWORD);   // browser firmware upload, password protected
#endif
  server.begin();
#endif

  tBeacon = millis() + 1500 + rnd32() % 2000;
  tPresence = millis() + PRESENCE_MS;
  MLOG("%s up, id %08x, SSID %s ch %d\n", nodeName, (unsigned)myId, MESH_SSID, MESH_CHANNEL);
}

void loop() {
#if !RELAY_ONLY
  dns.processNextRequest();
  server.handleClient();
#endif
  processRx();
  txPump();

  uint32_t now = millis();
  if ((int32_t)(now - tBeacon) >= 0) {
    char bt[24];
    snprintf(bt, sizeof(bt), "%lu;%u", (unsigned long)meshClock.now(now), (unsigned)vccMv());
    meshSend(T_BEACON, myId, 0, 0, nodeName, bt, 0, BEACON_TTL);
    tBeacon = now + BEACON_MS + rnd32() % 1000;
  }
  bool periodic = (int32_t)(now - tPresence) >= 0;
  if (periodic) tPresence = now + PRESENCE_MS;
  for (auto& u : users) {
    if (!u.id || u.node != myId) continue;
    if (now - u.seen > USER_TTL_MS) continue;
    if (u.needPresence || periodic) {
      u.needPresence = false;
      uint8_t th[8]; char hex[17] = "";
      if (authTab.lookup(u.id, th)) hashToHex(th, hex);
      meshSend(T_PRESENCE, u.id, 0, 0, u.name, hex);
    }
  }
  bn::Mail* due = mailbox.due(now);               // back-off retries of our own undelivered DMs
  if (due) {
    meshSend(T_DM, due->from, due->to, due->mid, due->name, due->text, due->ts, MESH_TTL, due->prio);
    mailbox.retried(due, now);                    // at most one per loop pass
  }
  if (stateDirty && now - lastSave > STATE_SAVE_MS) saveState();
  if (now - tRebase > 3600000UL) { meshClock.rebase(now); tRebase = now; }
  if (rep.active && (int32_t)(now - rep.next) >= 0) {
    if (rep.left == 0) rep.active = false;
    else {
      rep.left--; rep.next = now + 30000UL;
      meshSend(T_ALERT, rep.from, 0, rep.mid, rep.name, rep.text);
    }
  }
#if SOS_PIN >= 0
  static uint32_t sosSince = 0; static bool sosFired = false;
  if (digitalRead(SOS_PIN) == LOW) {
    if (!sosSince) sosSince = now ? now : 1;
    if (!sosFired && now - sosSince > 2000) {      // hold 2 s
      sosFired = true;
      char t[TEXT_MAX_LEN + 1]; snprintf(t, sizeof(t), "%s %s", SOS_TEXT, nodeName);
      raiseAlert(myId, nodeName, t);
    }
  } else { sosSince = 0; sosFired = false; }
#endif
#if LED_PIN >= 0
  static uint32_t ledOff = 0;
  if (millis() - ledOff > 40) { led(false); ledOff = millis(); }
#endif
  yield();
}
