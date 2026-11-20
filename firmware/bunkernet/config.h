#pragma once
// ===================== BunkerNet configuration =====================
// Every node must use the SAME MESH_CHANNEL and MESH_PSK.
//
// Don't edit secrets in this file. Run tools/new-psk.sh (or create
// firmware/bunkernet/config.local.h by hand): any #define placed there overrides
// the defaults below, and the file is git-ignored.
#if __has_include("config.local.h")
#include "config.local.h"
#endif

// Wi-Fi network that phones/laptops see (all nodes share it, so devices roam).
#ifndef MESH_SSID
#define MESH_SSID      "BunkerNet"
#endif
#ifndef AP_PASSWORD
#define AP_PASSWORD    ""          // "" = open network. Or 8+ chars for WPA2 (client link only).
#endif
#ifndef MAX_CLIENTS
#define MAX_CLIENTS    6           // ESP8266 soft-AP supports up to 8; ESP-01 RAM likes fewer.
#endif

// ESP-NOW backbone + AP must share one channel (1..13). Pick the least crowded.
#ifndef MESH_CHANNEL
#define MESH_CHANNEL   1
#endif

// Pre-shared key. Every mesh packet is encrypted + authenticated (ChaCha20-Poly1305).
// The default is PUBLIC (it is in the repo): always override it in config.local.h.
#ifndef MESH_PSK
#define MESH_PSK       "change-this-to-a-long-secret-phrase"
#endif

#ifndef MESH_TTL
#define MESH_TTL       8           // max hops a packet travels
#endif
#ifndef BEACON_TTL
#define BEACON_TTL     4           // hops a node announcement travels (limits periodic traffic)
#endif
#define BEACON_MS      10000UL     // node announce interval
#define PRESENCE_MS    15000UL     // user presence re-announce interval
#define USER_TTL_MS    50000UL     // user considered offline after this
#define NODE_TTL_MS    35000UL     // node considered gone after this

// Optional status LED (ESP-01S has one on GPIO2, active low). -1 to disable.
#ifndef LED_PIN
#define LED_PIN        -1
#endif

// Optional SOS button: wire a push button between this GPIO and GND, hold 2 s to raise
// a mesh-wide alert. ESP-01: GPIO2 works (don't hold the button while powering up). -1 = off.
#ifndef SOS_PIN
#define SOS_PIN        -1
#endif

// Text of the alert raised by the SOS button (followed by the node name).
#ifndef SOS_TEXT
#define SOS_TEXT       "SOS button pressed at"
#endif

// Range experiments, off by default. See docs/TESTING.md before relying on them.
#ifndef TX_POWER_DBM
#define TX_POWER_DBM   20.5        // 0..20.5 dBm
#endif
#ifndef MESH_PHY_11B
#define MESH_PHY_11B   0           // 1 = slower 802.11b radio mode, may reach further
#endif

// Optional browser firmware upload at http://192.168.4.1/update. Disabled unless you define a
// password (in config.local.h). Needs a board with 2 MB+ flash (NodeMCU, Wemos D1), not a 1 MB ESP-01.
// #define OTA_USER     "admin"
// #define OTA_PASSWORD "choose-a-long-password"
#ifndef OTA_USER
#define OTA_USER       "admin"
#endif

#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL   1
#endif
