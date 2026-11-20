#pragma once
// Hardware-independent BunkerNet logic. No Arduino or ESP headers here, so the same code
// is unit-tested on a normal computer (see tests/native) and runs on the ESP8266.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BN_NAME_MAX 23    // bytes, display names / data keys / node names
#define BN_TEXT_MAX 176   // bytes, message text (19 header + 23 name + 176 text <= 219 payload)

namespace bn {

// Longest prefix of s (at most max bytes) that does not cut a UTF-8 character in half.
inline size_t utf8_fit(const char* s, size_t len, size_t max) {
  size_t n = len < max ? len : max;
  while (n > 0 && n < len && (((uint8_t)s[n]) & 0xC0) == 0x80) n--;
  return n;
}

// ---------------------------------------------------------------------------
// Replay / duplicate protection.
// Every packet has a nonce (origin, boot, seq). `boot` is a counter that grows on every
// reboot of the origin, `seq` grows within a boot. Per origin we keep the highest seq and a
// 32-wide bitmap, which both drops flood duplicates and rejects recorded packets that are
// replayed later. An origin that stays silent for TTL_MS is forgotten, so a re-flashed
// node whose boot counter restarted low is accepted again after that time.
// ---------------------------------------------------------------------------
struct ReplayEnt { uint32_t origin, boot, maxSeq, bits, seen; bool used; };

template <size_t N>
class ReplayTable {
 public:
  static constexpr uint32_t TTL_MS = 600000UL;

  // true if the packet is fresh. Does not record anything (call before the expensive crypto).
  bool check(uint32_t origin, uint32_t boot, uint32_t seq, uint32_t now) const {
    const ReplayEnt* e = find(origin);
    if (!e || expired(*e, now)) return true;
    if (boot < e->boot) return false;       // replay from an older boot
    if (boot > e->boot) return true;        // node rebooted
    if (seq > e->maxSeq) return true;
    uint32_t d = e->maxSeq - seq;
    if (d >= 32) return false;              // too old to tell, treat as replay
    return !((e->bits >> d) & 1u);
  }

  // record an authenticated packet
  void commit(uint32_t origin, uint32_t boot, uint32_t seq, uint32_t now) {
    ReplayEnt* e = find(origin);
    if (!e) e = alloc(now);
    if (e->origin != origin || !e->used || expired(*e, now)) {
      e->used = true; e->origin = origin; e->boot = boot; e->maxSeq = seq; e->bits = 1; e->seen = now;
      return;
    }
    if (boot < e->boot) return;
    if (boot > e->boot) { e->boot = boot; e->maxSeq = seq; e->bits = 1; }
    else if (seq > e->maxSeq) {
      uint32_t shift = seq - e->maxSeq;
      e->bits = shift >= 32 ? 0 : (e->bits << shift);
      e->bits |= 1u;
      e->maxSeq = seq;
    } else {
      uint32_t d = e->maxSeq - seq;
      if (d < 32) e->bits |= (1u << d);
    }
    e->seen = now;
  }

 private:
  ReplayEnt t_[N] = {};
  static bool expired(const ReplayEnt& e, uint32_t now) { return now - e.seen > TTL_MS; }
  const ReplayEnt* find(uint32_t origin) const {
    for (size_t i = 0; i < N; i++) if (t_[i].used && t_[i].origin == origin) return &t_[i];
    return nullptr;
  }
  ReplayEnt* find(uint32_t origin) {
    for (size_t i = 0; i < N; i++) if (t_[i].used && t_[i].origin == origin) return &t_[i];
    return nullptr;
  }
  ReplayEnt* alloc(uint32_t now) {
    ReplayEnt* oldest = &t_[0];
    for (size_t i = 0; i < N; i++) {
      if (!t_[i].used) return &t_[i];
      if (now - t_[i].seen > now - oldest->seen) oldest = &t_[i];
    }
    return oldest;
  }
};

// ---------------------------------------------------------------------------
// Identity table: user id -> truncated hash of that user's secret token.
// First claim wins (trust on first use), then the same hash must be presented.
// ---------------------------------------------------------------------------
struct AuthEnt { uint32_t id; uint8_t h[8]; bool used; };

template <size_t N>
class AuthTable {
 public:
  enum Result { OK, NEW, BAD };
  Result check(uint32_t id, const uint8_t* h) const {
    const AuthEnt* e = find(id);
    if (!e) return NEW;
    return memcmp(e->h, h, 8) == 0 ? OK : BAD;
  }
  // remember a claim (no-op if the id is already known). Returns true if added.
  bool learn(uint32_t id, const uint8_t* h) {
    if (find(id)) return false;
    AuthEnt* e = nullptr;
    for (size_t i = 0; i < N; i++) if (!t_[i].used) { e = &t_[i]; break; }
    if (!e) { e = &t_[rr_]; rr_ = (rr_ + 1) % N; }       // full: replace round-robin
    e->used = true; e->id = id; memcpy(e->h, h, 8);
    return true;
  }
  bool lookup(uint32_t id, uint8_t* out) const {
    const AuthEnt* e = find(id);
    if (!e) return false;
    memcpy(out, e->h, 8);
    return true;
  }
  AuthEnt* raw() { return t_; }
  static constexpr size_t size() { return N; }

 private:
  AuthEnt t_[N] = {};
  size_t rr_ = 0;
  const AuthEnt* find(uint32_t id) const {
    for (size_t i = 0; i < N; i++) if (t_[i].used && t_[i].id == id) return &t_[i];
    return nullptr;
  }
};

// ---------------------------------------------------------------------------
// Store-and-forward mailbox for direct messages.
// ---------------------------------------------------------------------------
struct Mail {
  bool used, delivered, mine, prio; uint8_t tries;
  uint32_t from, to, mid, ts, next, lastAck;
  char name[BN_NAME_MAX + 1];
  char text[BN_TEXT_MAX + 1];
};

static const uint16_t RETRY_S[] = {30, 60, 120, 300, 600, 1200, 1800, 3600, 3600, 7200, 7200, 14400};
static const size_t RETRY_N = sizeof(RETRY_S) / sizeof(RETRY_S[0]);

template <size_t N>
class Mailbox {
 public:
  Mail* find(uint32_t mid) {
    for (size_t i = 0; i < N; i++) if (m_[i].used && m_[i].mid == mid) return &m_[i];
    return nullptr;
  }
  // returns true if the message was held and is now marked delivered
  bool markDelivered(uint32_t mid) {
    Mail* m = find(mid);
    if (!m || m->delivered) return false;
    m->delivered = true;
    return true;
  }
  Mail* store(uint32_t from, uint32_t to, uint32_t mid, uint32_t ts, const char* name,
              const char* text, bool mine, uint32_t now, bool prio = false) {
    Mail* m = nullptr;
    for (size_t i = 0; i < N; i++) if (!m_[i].used) { m = &m_[i]; break; }
    if (!m) for (size_t i = 0; i < N; i++) if (m_[i].delivered) { m = &m_[i]; break; }
    if (!m) { m = &m_[rr_]; rr_ = (rr_ + 1) % N; }      // full of undelivered: drop round-robin
    memset(m, 0, sizeof(*m));
    m->used = true; m->mine = mine; m->prio = prio;
    m->from = from; m->to = to; m->mid = mid; m->ts = ts;
    m->next = now + RETRY_S[0] * 1000UL;
    strncpy(m->name, name, BN_NAME_MAX);
    strncpy(m->text, text, BN_TEXT_MAX);
    return m;
  }
  // an own, undelivered message whose re-send time has come
  Mail* due(uint32_t now) {
    for (size_t i = 0; i < N; i++) {
      Mail& m = m_[i];
      if (m.used && m.mine && !m.delivered && (int32_t)(now - m.next) >= 0) return &m;
    }
    return nullptr;
  }
  void retried(Mail* m, uint32_t now) {
    if (++m->tries >= RETRY_N) m->mine = false;          // give up re-flooding
    else m->next = now + RETRY_S[m->tries] * 1000UL;
  }
  Mail* raw() { return m_; }
  static constexpr size_t size() { return N; }

 private:
  Mail m_[N] = {};
  size_t rr_ = 0;
};

// ---------------------------------------------------------------------------
// Shared mesh clock. The nodes have no real-time clock, so they learn the time from the
// phones and laptops that talk to them (and from each other's beacons). Every source gets one
// vote (its newest reading), the median wins, and a single wrong device is outvoted.
// ---------------------------------------------------------------------------
class MeshClock {
 public:
  static constexpr uint32_t SAMPLE_TTL_MS = 30UL * 60UL * 1000UL;   // a reading is trusted this long

  // unixSec: the source's idea of the current Unix time. source: who said it (user or node id).
  void feed(uint32_t source, uint32_t unixSec, uint32_t nowMs) {
    if (unixSec < MIN_PLAUSIBLE) return;                 // unset device clock (1970, 2000, ...)
    S* e = nullptr;
    for (auto& x : s_) if (x.used && x.src == source) { e = &x; break; }
    if (!e) for (auto& x : s_) if (!x.used || nowMs - x.ms > SAMPLE_TTL_MS) { e = &x; break; }
    if (!e) {                                             // table full: replace the oldest
      e = &s_[0];
      for (auto& x : s_) if (nowMs - x.ms > nowMs - e->ms) e = &x;
    }
    e->used = true; e->src = source; e->unix = unixSec; e->ms = nowMs;
    recompute(nowMs);
  }
  bool known() const { return known_; }
  uint32_t now(uint32_t nowMs) const { return known_ ? base_ + (nowMs - baseMs_) / 1000 : 0; }
  // keep the millisecond counter from wrapping (call now and then, e.g. hourly)
  void rebase(uint32_t nowMs) { if (known_) { base_ = now(nowMs); baseMs_ = nowMs; } }
  // the timestamp to put on a message: the sender's own if it is plausible, else the mesh time
  uint32_t stamp(uint32_t clientTs, uint32_t nowMs) const {
    if (!known_) return clientTs;
    uint32_t n = now(nowMs);
    int64_t d = (int64_t)clientTs - (int64_t)n;
    if (clientTs == 0 || d > 300 || d < -300) return n;
    return clientTs;
  }

 private:
  static constexpr uint32_t MIN_PLAUSIBLE = 1700000000UL;  // late 2023, anything earlier is a reset clock
  struct S { bool used; uint32_t src, unix, ms; };
  S s_[8] = {};
  bool known_ = false;
  uint32_t base_ = 0, baseMs_ = 0, baseSrc_ = 0;

  void setTo(uint32_t unixSec, uint32_t nowMs, uint32_t src) { known_ = true; base_ = unixSec; baseMs_ = nowMs; baseSrc_ = src; }

  void recompute(uint32_t nowMs) {
    uint32_t v[8], src[8]; size_t n = 0;
    for (auto& x : s_) {
      if (!x.used) continue;
      uint32_t age = nowMs - x.ms;
      if (age > SAMPLE_TTL_MS) { x.used = false; continue; }
      v[n] = x.unix + age / 1000; src[n] = x.src; n++;
    }
    if (n == 0) return;
    for (size_t i = 1; i < n; i++)                        // insertion sort, keeping sources aligned
      for (size_t j = i; j > 0 && v[j] < v[j - 1]; j--) {
        uint32_t t = v[j]; v[j] = v[j - 1]; v[j - 1] = t;
        t = src[j]; src[j] = src[j - 1]; src[j - 1] = t;
      }
    size_t mi = n / 2;
    uint32_t med = v[mi];
    if (!known_) { setTo(med, nowMs, src[mi]); return; }
    uint32_t cur = now(nowMs);
    uint32_t diff = med > cur ? med - cur : cur - med;
    if (diff < 2) return;                                 // already right
    size_t agree = 0;
    for (size_t i = 0; i < n; i++) { uint32_t d = v[i] > med ? v[i] - med : med - v[i]; if (d <= 60) agree++; }
    if (agree >= 2 || (n == 1 && src[0] == baseSrc_)) setTo(med, nowMs, src[mi]);
  }
};

// ---------------------------------------------------------------------------
// Relay damping for periodic, self-repairing traffic (beacons, presence).
// Chat, DMs, data and alerts are always relayed. `rnd` is a random number.
// ---------------------------------------------------------------------------
inline bool relay_damped(uint8_t dupesHeard, uint32_t rnd) {
  return dupesHeard >= 2 && (rnd & 1u) == 0;
}

}  // namespace bn
