#!/usr/bin/env bash
# Generate firmware/bunkernet/config.local.h with a fresh random mesh key.
# Every node of one mesh must be built with the SAME file. Keep a backup of it.
set -euo pipefail
out="$(dirname "$0")/../firmware/bunkernet/config.local.h"
if [ -e "$out" ] && [ "${1:-}" != "--force" ]; then
  echo "$out already exists (use --force to replace it; this changes the mesh key!)" >&2
  exit 1
fi
psk="$(od -An -N24 -tx1 /dev/urandom | tr -d " \n")"
cat > "$out" <<CFG
// Local, secret configuration. Not committed to git.
#define MESH_PSK "$psk"
// #define MESH_SSID "BunkerNet"
// #define MESH_CHANNEL 1
// #define AP_PASSWORD ""
// #define SOS_PIN 2
CFG
echo "Wrote $out"
