// BunkerNet - offline mesh network for ESP8266 / ESP-01
//
// Backbone : ESP-NOW broadcast flooding (TTL + dedupe), encrypted with ChaCha20-Poly1305
// Clients  : each node is a Wi-Fi AP (same SSID everywhere) + captive portal web app
// Features : group chat, direct messages with delivery receipts, shared data board,
//            node map. Same firmware on every node.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <espnow.h>
#include <bearssl/bearssl.h>
#include "config.h"
#include "web_ui.h"

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
#define NAME_MAX_LEN 15
#define TEXT_MAX_LEN 140

enum : uint8_t { T_CHAT = 1, T_DM = 2, T_PRESENCE = 3, T_BEACON = 4, T_ACK = 5, T_DATA = 6, T_ALERT = 7 };

#define RXQ_N   8
#define TXQ_N   16
#define SEEN_N  96
#define LOG_N   28
#define USERS_N 20
#define NODES_N 16
#define DATA_N  16
#define ALERT_N 4
#define MAIL_N  12

// ---------------------------------------------------------------- state
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint32_t myId, bootId, seqNo = 0;
static uint8_t  meshKey[32];
static char     nodeName[NAME_MAX_LEN + 1];

ESP8266WebServer server(80);
DNSServer        dns;

// receive queue (filled from the ESP-NOW callback, drained in loop())
struct RxPkt { uint8_t len; uint8_t d[PKT_MAX]; };
static RxPkt rxq[RXQ_N];
static volatile uint8_t rxHead = 0, rxTail = 0;

// transmit queue with per-packet send time (jitter avoids collisions)
struct TxPkt { bool used; uint8_t len; uint32_t at; uint8_t d[PKT_MAX]; };
static TxPkt txq[TXQ_N];
static uint32_t lastTx = 0;

static uint32_t seenHash[SEEN_N];
static uint8_t  seenIdx = 0;

// message log shown to local clients
struct LogEnt {
  uint32_t lid; uint8_t type; bool acked;
  uint32_t from, to, mid, ts;
  char name[NAME_MAX_LEN + 1];
  char text[TEXT_MAX_LEN + 1];
};
static LogEnt  logRing[LOG_N];
static uint32_t logCount = 0;

// Store-and-forward mailbox: every node keeps a copy of each direct message until a
// delivery receipt for it is seen. The recipient can then collect it from ANY node whenever
// they next appear, even hours later or after roaming. The node that originated a message
// also re-floods it on a back-off schedule so nodes that missed it (down, out of range) catch up.
struct Mail {
  bool used, delivered, mine; uint8_t tries;
  uint32_t from, to, mid, ts, next, lastAck;
  char name[NAME_MAX_LEN + 1];
  char text[TEXT_MAX_LEN + 1];
};
static Mail mailbox[MAIL_N];
static uint8_t mailRR = 0;
static const uint16_t RETRY_S[] = {30, 60, 120, 300, 600, 1200, 1800, 3600, 3600, 7200, 7200, 14400};
#define RETRY_N (sizeof(RETRY_S) / sizeof(RETRY_S[0]))

struct User {
  uint32_t id; uint32_t node; uint32_t seen; bool needPresence;
  char name[NAME_MAX_LEN + 1];
};
static User users[USERS_N];

struct NodeEnt { uint32_t id; uint32_t seen; uint8_t hops; char name[NAME_MAX_LEN + 1]; };
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

static uint32_t tBeacon = 0, tPresence = 0;

#define MLOG(...) do { if (DEBUG_SERIAL) Serial.printf(__VA_ARGS__); } while (0)

// ---------------------------------------------------------------- helpers
static void put32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t get32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint32_t rnd32() { return RANDOM_REG32; }

static uint32_t fnv(const uint8_t* p, size_t n) {
  uint32_t h = 2166136261UL;
  while (n--) { h ^= *p++; h *= 16777619UL; }
  return h;
}

// copy at most max bytes without cutting a UTF-8 character in half
static void copyUtf8(char* dst, const char* src, size_t srcLen, size_t max) {
  size_t n = srcLen < max ? srcLen : max;
  while (n > 0 && n < srcLen && (((uint8_t)src[n]) & 0xC0) == 0x80) n--;
  memcpy(dst, src, n);
  dst[n] = 0;
}

static bool isSeen(uint32_t h) {
  for (uint8_t i = 0; i < SEEN_N; i++) if (seenHash[i] == h) return true;
  return false;
}
static void markSeen(uint32_t h) { seenHash[seenIdx] = h; seenIdx = (seenIdx + 1) % SEEN_N; }

static void led(bool on) {
#if LED_PIN >= 0
  digitalWrite(LED_PIN, on ? LOW : HIGH);
#endif
}

// ---------------------------------------------------------------- tx queue
static void txEnqueue(const uint8_t* d, uint8_t len, uint32_t at) {
  for (uint8_t i = 0; i < TXQ_N; i++) {
    if (!txq[i].used) {
      txq[i].used = true; txq[i].len = len; txq[i].at = at;
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
      esp_now_send((uint8_t*)BCAST, txq[i].d, txq[i].len);
      txq[i].used = false; lastTx = now; led(true);
      return;
    }
  }
}

// ---------------------------------------------------------------- mesh send
static uint32_t meshSend(uint8_t type, uint32_t from, uint32_t to, uint32_t mid,
                         const char* name, const char* text, uint32_t ts = 0) {
  uint8_t pt[PT_MAX];
  size_t nl = strlen(name); if (nl > NAME_MAX_LEN) nl = NAME_MAX_LEN;
  size_t tl = strlen(text); if (tl > TEXT_MAX_LEN) tl = TEXT_MAX_LEN;
  if (mid == 0) mid = rnd32() | 1;
  pt[0] = type; pt[1] = MESH_TTL;
  put32(pt + 2, from); put32(pt + 6, to); put32(pt + 10, mid); put32(pt + 14, ts);
  pt[18] = (uint8_t)nl;
  memcpy(pt + PT_HDR, name, nl);
  memcpy(pt + PT_HDR + nl, text, tl);
  size_t n = PT_HDR + nl + tl;

  uint8_t pkt[PKT_MAX];
  pkt[0] = MAGIC; pkt[OFF_TTL] = MESH_TTL;
  seqNo++;
  put32(pkt + OFF_NONCE, myId); put32(pkt + OFF_NONCE + 4, bootId); put32(pkt + OFF_NONCE + 8, seqNo);
  pkt[OFF_LEN] = (uint8_t)n;
  memcpy(pkt + OFF_PAYLOAD, pt, n);
  uint8_t tag[TAG_LEN];
  br_poly1305_ctmul_run(meshKey, pkt + OFF_NONCE, pkt + OFF_PAYLOAD, n,
                        pkt + OFF_NONCE, 12, tag, br_chacha20_ct_run, 1);
  memcpy(pkt + OFF_PAYLOAD + n, tag, TAG_LEN);

  markSeen(fnv(pkt + OFF_NONCE, 12));
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
  if (node == myId && strncmp(u->name, name, NAME_MAX_LEN) != 0) u->needPresence = true;
  copyUtf8(u->name, name, strlen(name), NAME_MAX_LEN);
  u->node = node; u->seen = now;
  return u;
}

static bool userIsLocal(uint32_t id) {
  User* u = findUser(id);
  return u && u->node == myId && millis() - u->seen < 120000UL;
}

static void touchNode(uint32_t id, const char* name, uint8_t hops) {
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
  e->hops = hops; e->seen = now;
}

static void setData(uint32_t from, const char* k, const char* v, uint32_t ts) {
  DataEnt* e = nullptr;
  for (auto& d : board) if (d.used && strcmp(d.k, k) == 0) { e = &d; break; }
  if (!e) for (auto& d : board) if (!d.used) { e = &d; break; }
  if (!e) e = &board[rnd32() % DATA_N];          // board full: replace a random entry
  e->used = true; e->from = from; e->ts = ts;
  strncpy(e->k, k, NAME_MAX_LEN); e->k[NAME_MAX_LEN] = 0;
  strncpy(e->v, v, sizeof(e->v) - 1); e->v[sizeof(e->v) - 1] = 0;
  dataVer++;
}

static LogEnt* addLog(uint8_t type, uint32_t from, uint32_t to, uint32_t mid,
                      const char* name, const char* text, uint32_t ts = 0) {
  LogEnt& e = logRing[logCount % LOG_N];
  logCount++;
  e.lid = logCount; e.type = type; e.acked = false;
  e.from = from; e.to = to; e.mid = mid; e.ts = ts;
  strncpy(e.name, name, NAME_MAX_LEN); e.name[NAME_MAX_LEN] = 0;
  strncpy(e.text, text, TEXT_MAX_LEN); e.text[TEXT_MAX_LEN] = 0;
  return &e;
}

static Mail* findMail(uint32_t mid) {
  for (auto& m : mailbox) if (m.used && m.mid == mid) return &m;
  return nullptr;
}

static void markDelivered(uint32_t mid) {
  Mail* m = findMail(mid);
  if (m) m->delivered = true;
}

static Mail* storeMail(uint32_t from, uint32_t to, uint32_t mid, uint32_t ts,
                       const char* name, const char* text, bool mine) {
  Mail* m = nullptr;
  for (auto& x : mailbox) if (!x.used) { m = &x; break; }
  if (!m) for (auto& x : mailbox) if (x.delivered) { m = &x; break; }
  if (!m) { m = &mailbox[mailRR]; mailRR = (mailRR + 1) % MAIL_N; }   // full: drop round-robin
  memset(m, 0, sizeof(*m));
  m->used = true; m->mine = mine;
  m->from = from; m->to = to; m->mid = mid; m->ts = ts;
  m->next = millis() + RETRY_S[0] * 1000UL;
  strncpy(m->name, name, NAME_MAX_LEN);
  strncpy(m->text, text, TEXT_MAX_LEN);
  return m;
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

// ---------------------------------------------------------------- rx handling
static void handlePlain(const uint8_t* pt, size_t n, uint8_t ttl, uint32_t origin) {
  if (n < PT_HDR) return;
  uint8_t type = pt[0], ittl = pt[1];
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
      addLog(type, from, to, mid, name, text, ts);
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
      storeMail(from, to, mid, ts, name, text, false);
      addLog(T_DM, from, to, mid, name, text, ts);
      break;
    }
    case T_PRESENCE:
      touchUser(from, name, origin);
      break;
    case T_BEACON:
      touchNode(from, name, hops);
      break;
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
    bool ok = false;
    if (len >= OFF_PAYLOAD + TAG_LEN && d[0] == MAGIC) {
      uint8_t n = d[OFF_LEN];
      uint32_t origin = get32(d + OFF_NONCE);
      uint32_t h = fnv(d + OFF_NONCE, 12);
      if (len == OFF_PAYLOAD + n + TAG_LEN && n >= PT_HDR && origin != myId && !isSeen(h)) {
        uint8_t buf[PT_MAX], tag[TAG_LEN];
        memcpy(buf, d + OFF_PAYLOAD, n);
        br_poly1305_ctmul_run(meshKey, d + OFF_NONCE, buf, n, d + OFF_NONCE, 12,
                              tag, br_chacha20_ct_run, 0);
        uint8_t diff = 0;
        for (uint8_t i = 0; i < TAG_LEN; i++) diff |= tag[i] ^ d[OFF_PAYLOAD + n + i];
        if (diff == 0) {
          ok = true;
          markSeen(h);
          uint8_t ttl = d[OFF_TTL];
          if (ttl > 1) {                      // relay with random delay
            d[OFF_TTL] = ttl - 1;
            txEnqueue(d, len, millis() + 5 + rnd32() % 40);
          }
          handlePlain(buf, n, ttl, origin);
          led(true);
        }
      }
    }
    (void)ok;
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

static void registerClient() {
  uint32_t me = argHex("me");
  if (!me) return;
  String nm = server.arg("name");
  touchUser(me, nm.c_str(), myId);
}

static void handlePoll() {
  registerClient();
  uint32_t me = argHex("me");
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
  o += ",\"msgs\":[";
  uint32_t first = logCount > LOG_N ? logCount - LOG_N + 1 : 1;
  if (since + 1 > first) first = since + 1;
  bool comma = false;
  for (uint32_t lid = first; lid <= logCount; lid++) {
    LogEnt& e = logRing[(lid - 1) % LOG_N];
    bool vis = e.type == T_CHAT || (e.type == T_DM && (e.from == me || e.to == me)) ||
               (e.type == T_ACK && e.to == me);
    if (!vis) continue;
    if (comma) o += ',';
    comma = true;
    o += "{\"t\":"; o += e.type;
    hex8(h, e.from); o += ",\"f\":\""; o += h;
    hex8(h, e.to);   o += "\",\"to\":\""; o += h;
    hex8(h, e.mid);  o += "\",\"m\":\""; o += h;
    o += "\",\"s\":"; o += e.ts;
    o += ",\"n\":"; jesc(o, e.name);
    o += ",\"x\":"; jesc(o, e.text);
    o += '}';
    flushJson(o);
  }
  // store-and-forward: DMs that were held for this user while they were away
  for (auto& mm : mailbox) {
    if (!mm.used || mm.delivered || mm.to != me) continue;
    sendAck(me, mm.from, mm.mid);
    if (comma) o += ',';
    comma = true;
    o += "{\"t\":2";
    hex8(h, mm.from); o += ",\"f\":\""; o += h;
    hex8(h, mm.to);   o += "\",\"to\":\""; o += h;
    hex8(h, mm.mid);  o += "\",\"m\":\""; o += h;
    o += "\",\"s\":"; o += mm.ts;
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
  registerClient();
  uint32_t me = argHex("me"), to = argHex("to");
  if (!me || !server.hasArg("x")) { server.send(400, "text/plain", "bad"); return; }
  String nm = server.arg("name"), x = server.arg("x");
  char name[NAME_MAX_LEN + 1], text[TEXT_MAX_LEN + 1];
  copyUtf8(name, nm.c_str(), nm.length(), NAME_MAX_LEN);
  copyUtf8(text, x.c_str(), x.length(), TEXT_MAX_LEN);
  if (!text[0]) { server.send(400, "text/plain", "empty"); return; }
  uint8_t type = to ? T_DM : T_CHAT;
  uint32_t ts = (uint32_t)strtoul(server.arg("ts").c_str(), nullptr, 10);
  uint32_t mid = meshSend(type, me, to, 0, name, text, ts);
  addLog(type, me, to, mid, name, text, ts);
  if (type == T_DM) storeMail(me, to, mid, ts, name, text, true);
  server.send(200, "text/plain", "ok");
}

static void handlePut() {
  registerClient();
  uint32_t me = argHex("me");
  if (!me || !server.hasArg("k")) { server.send(400, "text/plain", "bad"); return; }
  String nm = server.arg("name"), k = server.arg("k"), v = server.arg("v");
  char name[NAME_MAX_LEN + 1], key[NAME_MAX_LEN + 1], val[48];
  copyUtf8(name, nm.c_str(), nm.length(), NAME_MAX_LEN);
  copyUtf8(key, k.c_str(), k.length(), NAME_MAX_LEN);
  copyUtf8(val, v.c_str(), v.length(), 47);
  if (!key[0]) { server.send(400, "text/plain", "bad"); return; }
  uint32_t ts = (uint32_t)strtoul(server.arg("ts").c_str(), nullptr, 10);
  setData(me, key, val, ts);
  meshSend(T_DATA, me, 0, 0, key, val, ts);
  server.send(200, "text/plain", "ok");
}

static void handleAlert() {
  registerClient();
  uint32_t me = argHex("me");
  if (!me || !server.hasArg("x")) { server.send(400, "text/plain", "bad"); return; }
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
  if (!me || !mid) { server.send(400, "text/plain", "bad"); return; }
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
  o += ",\"nodes\":[{\"id\":\""; o += (h + 4); o += "\",\"n\":"; jesc(o, nodeName); o += ",\"h\":0,\"a\":0}";
  uint32_t now = millis();
  for (auto& n : nodes) {
    if (!n.id || now - n.seen > NODE_TTL_MS) continue;
    hex8(h, n.id);
    o += ",{\"id\":\""; o += (h + 4); o += "\",\"n\":"; jesc(o, n.name);
    o += ",\"h\":"; o += n.hops; o += ",\"a\":"; o += (now - n.seen) / 1000; o += '}';
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
  bootId = rnd32();
  snprintf(nodeName, sizeof(nodeName), "Node-%04X", (unsigned)(myId & 0xFFFF));

  br_sha256_context sc;
  br_sha256_init(&sc);
  br_sha256_update(&sc, MESH_PSK, strlen(MESH_PSK));
  br_sha256_out(&sc, meshKey);

  WiFi.persistent(false);
  WiFi.setAutoConnect(false);
  WiFi.mode(WIFI_AP_STA);
  IPAddress ip(192, 168, 4, 1);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(MESH_SSID, AP_PASSWORD[0] ? AP_PASSWORD : nullptr, MESH_CHANNEL, 0, MAX_CLIENTS);
  WiFi.disconnect();

  if (esp_now_init() != 0) { MLOG("esp_now_init failed\n"); delay(1000); ESP.restart(); }
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_add_peer((uint8_t*)BCAST, ESP_NOW_ROLE_COMBO, MESH_CHANNEL, NULL, 0);
  esp_now_register_recv_cb(onRecv);

  dns.start(53, "*", ip);
  server.on("/", handleRoot);
  server.on("/poll", handlePoll);
  server.on("/send", handleSend);
  server.on("/put", handlePut);
  server.on("/nodes", handleNodes);
  server.on("/alert", handleAlert);
  server.on("/alertclear", handleAlertClear);
  server.onNotFound(handleNotFound);
  server.begin();

  tBeacon = millis() + 1500 + rnd32() % 2000;
  tPresence = millis() + PRESENCE_MS;
  MLOG("%s up, id %08x, SSID %s ch %d\n", nodeName, (unsigned)myId, MESH_SSID, MESH_CHANNEL);
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
  processRx();
  txPump();

  uint32_t now = millis();
  if ((int32_t)(now - tBeacon) >= 0) {
    meshSend(T_BEACON, myId, 0, 0, nodeName, "");
    tBeacon = now + BEACON_MS + rnd32() % 1000;
  }
  bool periodic = (int32_t)(now - tPresence) >= 0;
  if (periodic) tPresence = now + PRESENCE_MS;
  for (auto& u : users) {
    if (!u.id || u.node != myId) continue;
    if (now - u.seen > USER_TTL_MS) continue;
    if (u.needPresence || periodic) {
      u.needPresence = false;
      meshSend(T_PRESENCE, u.id, 0, 0, u.name, "");
    }
  }
  for (auto& m : mailbox) {                       // back-off retries of our own undelivered DMs
    if (m.used && m.mine && !m.delivered && (int32_t)(now - m.next) >= 0) {
      meshSend(T_DM, m.from, m.to, m.mid, m.name, m.text, m.ts);
      if (++m.tries >= RETRY_N) m.mine = false;
      else m.next = now + RETRY_S[m.tries] * 1000UL;
      break;                                      // at most one per loop pass
    }
  }
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
      char t[TEXT_MAX_LEN + 1]; snprintf(t, sizeof(t), "SOS button pressed at %s", nodeName);
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
