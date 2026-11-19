# BunkerNet

[![CI](https://github.com/gumslone/bunkernet/actions/workflows/ci.yml/badge.svg)](https://github.com/gumslone/bunkernet/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

An offline, self-healing communication mesh for cheap **ESP-01 / ESP8266** modules. It works
with no internet, no router and no app install.

Place a few modules around a building and plug them in. Phones and laptops join the Wi-Fi
network `BunkerNet`, a page opens automatically, and everyone can chat, send private
messages, share data and raise alarms. It was designed for a bunker or "no infrastructure"
scenario, but it works equally well for a cabin, a festival or a blackout.

## Features

- **Group chat and private messages**, with delivery ticks (✓ sent, ✓✓ delivered).
- **Emergency alerts:** a red banner with sound and vibration on every device on every node,
  repeated for about 5 minutes so briefly-offline nodes still get it. Optional **hardware SOS
  button** (hold 2 s).
- **Shared data board:** small key/value entries such as `water_level = 40%`, readable and
  writable from the web page or through the HTTP API (for sensors and scripts).
- **Node map:** which nodes are alive and how many hops away, to help you place them.
- **Encrypted mesh:** every packet is ChaCha20-Poly1305 encrypted and authenticated with a
  pre-shared key.
- **Self-healing:** no routing tables and no links to break. If a node dies, packets take
  another path.
- **Roaming:** every node uses the same SSID and address (192.168.4.1), so a phone moves to
  the nearest node as you walk.
- **Zero dependencies:** only the stock ESP8266 Arduino core. The web app is embedded in the
  firmware, with no CDN calls and nothing fetched at runtime.

## How it works

```
 phone ──Wi-Fi──► node A ◄──ESP-NOW (encrypted flood)──► node B ──Wi-Fi──► laptop
                     ▲                                      ▲
                     └──────────── node C ──────────────────┘
```

- **Backbone:** ESP-NOW broadcast flooding on one fixed channel. Every packet carries a hop
  limit (TTL) and a unique id. Nodes remember recent ids to drop duplicates, then relay each
  new packet after a small random delay to avoid collisions.
- **Access:** every node is a soft access point on the same channel, with a captive portal
  that serves the web app. The page polls the node over HTTP, so any browser works.
- **Identity:** the browser generates a random id and a name, kept in local storage. Private
  messages are addressed to that id.
- **Delivery receipts:** a private message is acknowledged when the recipient's device
  actually fetches it, and the receipt floods back to the sender's node.

## Quick start

You need: one or more ESP-01 / ESP8266 boards, a 3.3V USB-serial adapter for the ESP-01
(or any NodeMCU/Wemos board with USB), and [PlatformIO](https://platformio.org/).

```bash
git clone https://github.com/gumslone/bunkernet.git
cd bunkernet

# 1. Generate YOUR secret mesh key (git-ignored). Back this file up!
./tools/new-psk.sh

# 2. Build and flash. Repeat with every module, using the same key.
pio run -t upload
```

Then power the modules, connect a phone to `BunkerNet`, and open any web page. The app opens
automatically. If it doesn't, go to <http://192.168.4.1>.

> **Always generate your own key.** The default key in `config.h` is public. Anyone can read
> the repo, so anyone with that key could read your mesh traffic.

### Flashing an ESP-01

The ESP-01 has no USB, so it needs an adapter (CH340/CP2102/FTDI set to **3.3V**):

| Adapter | ESP-01 |
|---------|--------|
| 3V3     | VCC and CH_PD (EN) |
| GND     | GND |
| TX      | RX |
| RX      | TX |
| GND     | **GPIO0**, only while powering up (enters the bootloader) |

The ESP-01 needs a stable supply of 300 mA or more, and weak USB-serial adapters often brown
out. Use the 3.3V pin of a good adapter or a separate regulator. Release GPIO0 after
flashing and power-cycle.

**Prefer a GUI?** [BugZapper](https://github.com/gumslone/BugZapper) is a small flasher for
ESP8266/ESP8285 modules with a built-in serial monitor and a bundled esptool, so there is
nothing to install. Build with `pio run`, then flash
`.pio/build/esp01_1m/firmware.bin` (or a release binary) at offset `0x0`. Use it to flash
many modules quickly and to read each node's serial log.

Other boards: `pio run -e nodemcuv2 -t upload` for NodeMCU. Add more targets to
[`platformio.ini`](platformio.ini) as needed.

### Arduino IDE

Open `firmware/bunkernet/bunkernet.ino`, choose *Generic ESP8266 Module* (flash 1MB), and
install the ESP8266 core. Create `config.local.h` next to it with
`#define MESH_PSK "your secret"`.

## Configuration

Defaults live in [`firmware/bunkernet/config.h`](firmware/bunkernet/config.h). Override any
of them in the git-ignored `config.local.h` (`tools/new-psk.sh` creates it):

| Setting | Default | Meaning |
|---------|---------|---------|
| `MESH_PSK` | public placeholder | Shared secret. **Must be identical on all nodes.** |
| `MESH_SSID` | `BunkerNet` | Wi-Fi name that devices see |
| `AP_PASSWORD` | `""` (open) | Optional WPA2 password for the phone-to-node link |
| `MESH_CHANNEL` | `1` | Wi-Fi channel for the whole mesh (pick a quiet one) |
| `MESH_TTL` | `8` | Max hops |
| `MAX_CLIENTS` | `6` | Devices per node |
| `SOS_PIN` | `-1` (off) | GPIO for a push button to GND (ESP-01: GPIO2) |
| `LED_PIN` | `-1` (off) | Activity LED (ESP-01S: GPIO2, active low) |

## Placement tips

- Expect roughly 10–25 m between nodes indoors, less through concrete or rebar.
- Mount nodes high and away from metal. Keep antennas clear.
- Use the **Nodes** tab to check that every node you care about is listed and not too many
  hops away.
- One node serves about 6 devices, so add nodes where people gather.

## Bunker and off-grid notes

- **Power:** an ESP-01 draws roughly 70–150 mA. Use a USB power bank, or a 12V battery with a
  proper 3.3V regulator. For long-term operation add a solar panel and a charge controller.
- **Spares:** flash and label 2–3 spare nodes now. Keep your `config.local.h`, this README
  and a USB-serial adapter together in a waterproof bag.
- **Offline rebuilds:** build once now, then back up this repository and the `~/.platformio`
  folder so you can re-flash with no network.
- **Reset:** history is only held in RAM, so a power cycle clears it. That is by design.

## HTTP API

Scripts and sensors on the mesh Wi-Fi can talk to any node. IDs are 32-bit hex strings you
choose (non-zero).

| Endpoint | Purpose |
|----------|---------|
| `POST /send` | `me`, `name`, `to` (`0` = everyone), `x` (text) |
| `POST /put` | `me`, `name`, `k`, `v`: set a data-board entry |
| `POST /alert`, `POST /alertclear` | `me`, `name`, `x` / `me`, `m` |
| `GET /poll` | `me`, `name`, `since`, `dv`, `av`: new messages, users, data, alerts |
| `GET /nodes` | Known nodes, hops and node stats |

Example: publish a sensor value from a script.

```bash
curl -d "me=0000a001&name=cellar&k=temp&v=14.2C" http://192.168.4.1/put
```

## Protocol

Packets are at most 250 bytes: `magic | ttl | origin(4) boot(4) seq(4) | len | ciphertext | tag(16)`.
The 12-byte nonce (origin, boot id, sequence number) is authenticated as additional data.
The payload holds the message type, original TTL, sender, recipient, message id, name and text.
Types: chat, DM, presence, beacon, ACK, data, alert. See
[`bunkernet.ino`](firmware/bunkernet/bunkernet.ino).

## Limits

- Text only, 140 characters per message. No files, voice or images.
- Range is plain 2.4 GHz Wi-Fi, extended only by adding nodes (hops).
- The hop counter (TTL) is not authenticated, so a nearby attacker can only affect how far a
  packet spreads, not its contents.
- The link between a phone and its node is open unless you set `AP_PASSWORD`. Only the node
  to node traffic is encrypted.
- No persistence. Messages are kept in RAM, about the last 28 per node.

## Development

CI (GitHub Actions) builds every PlatformIO environment on each push and pull request and
syntax-checks the embedded web UI script. Pushing a tag like `v0.1.0` builds all targets and
publishes a GitHub release with the firmware binaries and checksums. Release binaries use the
public default key, so they are for testing only.

```bash
pio run                 # build default target
pio run -e nodemcuv2    # build another target
pio device monitor      # serial log at 115200
```

## License

[MIT](LICENSE)
