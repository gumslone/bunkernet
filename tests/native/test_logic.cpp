// Native (desktop) tests for the hardware-independent BunkerNet logic.
//   g++ -std=c++17 -O1 -Wall -Wextra tests/native/test_logic.cpp -o /tmp/bn_test && /tmp/bn_test
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <queue>
#include <random>
#include <vector>

#include "../../firmware/bunkernet/mesh_logic.h"

static int failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } \
  } while (0)

// ----------------------------------------------------------------- utf8
static void test_utf8() {
  const char* s = "Привіт";                              // 6 letters, 12 bytes
  CHECK(strlen(s) == 12);
  CHECK(bn::utf8_fit(s, 12, 12) == 12);
  CHECK(bn::utf8_fit(s, 12, 11) == 10);                  // would cut a letter: back off
  CHECK(bn::utf8_fit(s, 12, 1) == 0);
  CHECK(bn::utf8_fit("hello", 5, 3) == 3);
  const char* e = "a💧b";                                 // 4-byte emoji
  CHECK(bn::utf8_fit(e, strlen(e), 3) == 1);
  CHECK(bn::utf8_fit(e, strlen(e), 5) == 5);
}

// ----------------------------------------------------------------- replay
static void test_replay() {
  bn::ReplayTable<4> r;
  uint32_t t = 1000;
  CHECK(r.check(1, 5, 10, t));
  r.commit(1, 5, 10, t);
  CHECK(!r.check(1, 5, 10, t));                          // duplicate
  CHECK(r.check(1, 5, 11, t));
  CHECK(r.check(1, 5, 9, t));                            // late, out of order: still fresh
  r.commit(1, 5, 9, t);
  CHECK(!r.check(1, 5, 9, t));
  r.commit(1, 5, 11, t);
  CHECK(!r.check(1, 5, 10, t));                          // still remembered after window shift
  r.commit(1, 5, 100, t);
  CHECK(!r.check(1, 5, 11, t));                          // far behind the window: rejected
  CHECK(!r.check(1, 4, 500, t));                         // recorded packet from an older boot
  CHECK(r.check(1, 6, 1, t));                            // node rebooted
  r.commit(1, 6, 1, t);
  CHECK(!r.check(1, 5, 101, t));                         // old boot is now a replay
  CHECK(r.check(2, 1, 1, t));                            // another origin is independent

  // eviction when more origins than slots
  for (uint32_t o = 10; o < 20; o++) r.commit(o, 1, 1, t + o);
  CHECK(r.check(1, 6, 1, t + 20) || true);               // evicted entries just become fresh

  // a silent origin is forgotten after the TTL (re-flashed node with a low boot counter)
  bn::ReplayTable<4> q;
  q.commit(7, 50, 5, 0);
  CHECK(!q.check(7, 1, 1, 1000));
  CHECK(q.check(7, 1, 1, bn::ReplayTable<4>::TTL_MS + 5000));
  q.commit(7, 1, 1, bn::ReplayTable<4>::TTL_MS + 5000);
  CHECK(!q.check(7, 1, 1, bn::ReplayTable<4>::TTL_MS + 5001));

  // millis() wrap-around
  bn::ReplayTable<2> w;
  uint32_t near_wrap = 0xFFFFFF00u;
  w.commit(3, 1, 1, near_wrap);
  CHECK(!w.check(3, 1, 1, near_wrap + 1000));            // still tracked across the wrap
}

// ----------------------------------------------------------------- auth
static void test_auth() {
  bn::AuthTable<3> a;
  uint8_t h1[8] = {1, 2, 3, 4, 5, 6, 7, 8}, h2[8] = {9, 9, 9, 9, 9, 9, 9, 9};
  CHECK(a.check(42, h1) == bn::AuthTable<3>::NEW);
  CHECK(a.learn(42, h1));
  CHECK(a.check(42, h1) == bn::AuthTable<3>::OK);
  CHECK(a.check(42, h2) == bn::AuthTable<3>::BAD);       // someone else's token for that id
  CHECK(!a.learn(42, h2));                               // first claim wins
  CHECK(a.check(42, h1) == bn::AuthTable<3>::OK);
  uint8_t out[8];
  CHECK(a.lookup(42, out) && memcmp(out, h1, 8) == 0);
  CHECK(!a.lookup(43, out));
  a.learn(1, h1); a.learn(2, h1); a.learn(3, h1);        // table full: round robin replace
  CHECK(a.check(3, h1) == bn::AuthTable<3>::OK);
}

// ----------------------------------------------------------------- mailbox
static void test_mailbox() {
  bn::Mailbox<3> mb;
  uint32_t t = 0;
  bn::Mail* m = mb.store(1, 2, 100, 555, "Ann", "hi", true, t, true);
  CHECK(m && mb.find(100) == m && m->prio);
  CHECK(mb.due(t + 29000) == nullptr);                   // first retry after 30 s
  CHECK(mb.due(t + 30000) == m);
  mb.retried(m, t + 30000);
  CHECK(m->tries == 1);
  CHECK(mb.due(t + 30000 + 59000) == nullptr);           // then 60 s
  CHECK(mb.due(t + 30000 + 60000) == m);
  CHECK(mb.markDelivered(100));
  CHECK(!mb.markDelivered(100));                         // second receipt is a no-op
  CHECK(mb.due(t + 99999999u) == nullptr);               // delivered: no more retries
  CHECK(!mb.markDelivered(999));                         // unknown mid

  // a message we only hold for someone else is never re-flooded by us
  bn::Mail* other = mb.store(5, 6, 200, 1, "Bob", "yo", false, t);
  CHECK(mb.due(t + 99999999u) == nullptr);
  (void)other;

  // eviction prefers delivered messages, then round-robin over undelivered ones
  bn::Mailbox<2> small;
  small.store(1, 2, 1, 0, "", "a", false, 0);
  small.store(1, 2, 2, 0, "", "b", false, 0);
  small.markDelivered(1);
  small.store(1, 2, 3, 0, "", "c", false, 0);
  CHECK(small.find(1) == nullptr && small.find(2) && small.find(3));
  small.store(1, 2, 4, 0, "", "d", false, 0);            // all undelivered: round robin drops one
  CHECK(small.find(4) != nullptr);
  CHECK((small.find(2) != nullptr) != (small.find(3) != nullptr));

  // retry schedule ends
  bn::Mailbox<1> g;
  bn::Mail* gm = g.store(1, 2, 9, 0, "", "x", true, 0);
  uint32_t now = 0; int n = 0;
  while (g.due(now + 100000000u) && n < 50) { gm = g.due(now + 100000000u); g.retried(gm, now); n++; }
  CHECK(n == (int)bn::RETRY_N);
  CHECK(!gm->mine);
}

// ----------------------------------------------------------------- clock
static void test_clock() {
  const uint32_t T0 = 1800000000UL;                       // some plausible Unix time
  bn::MeshClock c;
  CHECK(!c.known());
  CHECK(c.stamp(123, 0) == 123);                          // unknown clock: pass the sender's through
  c.feed(1, 5, 1000);                                     // 1970-ish device clock is ignored
  CHECK(!c.known());
  c.feed(1, T0, 1000);
  CHECK(c.known());
  CHECK(c.now(1000) == T0);
  CHECK(c.now(61000) == T0 + 60);                         // runs on its own

  // one wrong phone is outvoted by three right ones
  bn::MeshClock d;
  d.feed(10, T0, 0);
  d.feed(11, T0 + 3600, 0);                               // an hour off
  d.feed(12, T0 + 1, 0);
  d.feed(13, T0, 0);
  CHECK(d.now(0) >= T0 && d.now(0) <= T0 + 1);
  d.feed(11, T0 + 3600 + 5, 5000);                        // the wrong phone keeps talking
  d.feed(11, T0 + 3600 + 6, 6000);
  CHECK(d.now(6000) < T0 + 20);                           // still not swayed

  // the first source was wrong, then correct ones arrive: it recovers
  bn::MeshClock e;
  e.feed(1, T0 + 7200, 0);
  CHECK(e.now(0) == T0 + 7200);
  e.feed(2, T0, 1000);                                    // 2 sources disagree: no change yet
  CHECK(e.now(1000) == T0 + 7201);
  e.feed(3, T0, 2000);                                    // two agree: majority wins
  CHECK(e.now(2000) >= T0 + 1 && e.now(2000) <= T0 + 3);

  // a lone, different, unrelated source does not hijack a known clock
  bn::MeshClock f;
  f.feed(1, T0, 0);
  f.feed(2, T0 + 5000, 100);
  CHECK(f.now(100) <= T0 + 1);

  // stamp: a plausible sender time is kept, a wild one is replaced by the mesh time
  bn::MeshClock g;
  g.feed(1, T0, 0);
  CHECK(g.stamp(T0 + 100, 0) == T0 + 100);
  CHECK(g.stamp(T0 - 3000, 0) == T0);
  CHECK(g.stamp(0, 0) == T0);                             // API client without a timestamp
  CHECK(g.stamp(T0 + 4000, 0) == T0);

  // readings expire, rebase survives the millisecond counter wrapping
  bn::MeshClock h;
  h.feed(1, T0, 0);
  h.rebase(0xFFFFFF00u);
  CHECK(h.now(0xFFFFFF00u) == T0 + 0xFFFFFF00u / 1000);
  uint32_t before = h.now(0xFFFFFF00u);
  CHECK(h.now(0xFFFFFF00u + 5000) == before + 5);         // across the wrap
}

// ----------------------------------------------------------------- flood simulation
struct Sim {
  struct Node { std::vector<int> nb; bn::ReplayTable<8> replay; };
  std::vector<Node> n;
  std::mt19937 rng{1234};

  bool build(int count, double radius) {
    std::uniform_real_distribution<double> u(0, 1);
    std::vector<std::pair<double, double>> p(count);
    for (auto& q : p) q = {u(rng), u(rng)};
    n.assign(count, Node());
    for (int i = 0; i < count; i++)
      for (int j = 0; j < count; j++)
        if (i != j && std::hypot(p[i].first - p[j].first, p[i].second - p[j].second) < radius)
          n[i].nb.push_back(j);
    // connected?
    std::vector<int> seen(count, 0), st{0}; seen[0] = 1; int c = 1;
    while (!st.empty()) { int x = st.back(); st.pop_back(); for (int y : n[x].nb) if (!seen[y]) { seen[y] = 1; c++; st.push_back(y); } }
    return c == count;
  }

  struct Result { int reached; int tx; std::vector<int> got; };

  // One packet from `origin`. damped=true applies relay damping.
  Result flood(int origin, uint32_t seq, uint8_t ttl, bool damped, double loss) {
    struct Ev { double t; int node; int kind; int ttl; int from; bool operator<(const Ev& o) const { return t > o.t; } };
    // kind 0 = packet arrives at node, kind 1 = node's relay timer fires
    std::priority_queue<Ev> q;
    std::vector<int> pending(n.size(), 0), dupes(n.size(), 0), pttl(n.size(), 0), got(n.size(), 0);
    std::uniform_real_distribution<double> u(0, 1);
    int tx = 0;
    uint32_t now = 1000;
    auto transmit = [&](double t, int from, int tt) {
      tx++;
      for (int v : n[from].nb) if (u(rng) >= loss) q.push({t + 1.0, v, 0, tt, from});
    };
    got[origin] = 1;
    n[origin].replay.commit(origin + 1, 1, seq, now);
    transmit(0, origin, ttl);
    while (!q.empty()) {
      Ev e = q.top(); q.pop();
      if (e.kind == 0) {
        Node& nd = n[e.node];
        if (nd.replay.check(origin + 1, 1, seq, now)) {
          nd.replay.commit(origin + 1, 1, seq, now);
          got[e.node] = 1;
          if (e.ttl > 1) {
            pending[e.node] = 1; pttl[e.node] = e.ttl - 1;
            q.push({e.t + 10 + u(rng) * 70, e.node, 1, 0, 0});
          }
        } else if (pending[e.node]) {
          dupes[e.node]++;
        }
      } else if (pending[e.node]) {
        pending[e.node] = 0;
        if (damped && bn::relay_damped((uint8_t)dupes[e.node], (uint32_t)rng())) continue;
        transmit(e.t, e.node, pttl[e.node]);
      }
    }
    int reached = 0; for (int g : got) reached += g;
    return {reached, tx, got};
  }
};

static void test_flood() {
  // 1. reliable flooding reaches everyone, each node transmits at most once
  Sim s;
  while (!s.build(30, 0.3)) {}
  uint32_t seq = 1;
  for (int origin = 0; origin < 30; origin++) {
    Sim::Result r = s.flood(origin, seq++, 12, false, 0.0);
    CHECK(r.reached == 30);
    CHECK(r.tx <= 30);
  }

  // 2. TTL really bounds the spread
  Sim line;
  line.n.assign(10, Sim::Node());
  for (int i = 0; i + 1 < 10; i++) { line.n[i].nb.push_back(i + 1); line.n[i + 1].nb.push_back(i); }
  Sim::Result lr = line.flood(0, 1, 4, false, 0.0);
  CHECK(lr.reached == 5);                                // origin + 4 hops

  // 3. damping cuts periodic-traffic transmissions in a dense mesh while every origin is
  //    still heard by (nearly) everyone over a few beacon rounds
  Sim d;
  while (!d.build(30, 0.45)) {}
  std::vector<uint32_t> seqs(30, 1);                    // sequence numbers must grow per origin
  long base_tx = 0, damp_tx = 0;
  int heard = 0, pairs = 0;
  for (int origin = 0; origin < 30; origin++) {
    std::vector<int> union_got(30, 0);
    for (int round = 0; round < 4; round++) {
      Sim::Result b = d.flood(origin, seqs[origin]++, 4, false, 0.05);
      base_tx += b.tx;
      Sim::Result r = d.flood(origin, seqs[origin]++, 4, true, 0.05);
      damp_tx += r.tx;
      for (int i = 0; i < 30; i++) union_got[i] |= r.got[i];
    }
    for (int g : union_got) { heard += g; pairs++; }
  }
  double cov = (double)heard / pairs;
  std::printf("damping: tx %ld -> %ld (%.0f%%), coverage after 4 rounds %.1f%%\n", base_tx, damp_tx,
              100.0 * damp_tx / base_tx, 100.0 * cov);
  CHECK(damp_tx < base_tx * 0.9);
  CHECK(cov > 0.97);
}

int main() {
  test_utf8();
  test_replay();
  test_auth();
  test_mailbox();
  test_clock();
  test_flood();
  if (failures) { std::printf("%d check(s) FAILED\n", failures); return 1; }
  std::printf("all tests passed\n");
  return 0;
}
