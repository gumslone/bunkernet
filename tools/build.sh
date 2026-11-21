#!/usr/bin/env bash
# Build BunkerNet firmware and collect the binaries in dist/ (macOS and Linux).
#
#   ./tools/build.sh                 build the default target (esp01_1m)
#   ./tools/build.sh all             build every target
#   ./tools/build.sh esp01_relay     build one target
#   ./tools/build.sh all --test      run the native tests first
#   ./tools/build.sh --clean         remove old build output first
#
# Targets: esp01_1m (ESP-01/01S), esp01_relay (relay only, no Wi-Fi AP), nodemcuv2 (NodeMCU etc.)
# Output : dist/bunkernet-<target>-<version>.bin and dist/SHA256SUMS
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ALL_TARGETS="esp01_1m esp01_relay nodemcuv2"
LOCAL_CFG="firmware/bunkernet/config.local.h"

die()  { echo "error: $*" >&2; exit 1; }
warn() { echo "warning: $*" >&2; }
usage() { sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

targets=""
run_tests=0
clean=0
for arg in "$@"; do
  case "$arg" in
    -h|--help) usage 0 ;;
    --test)    run_tests=1 ;;
    --clean)   clean=1 ;;
    all)       targets="$ALL_TARGETS" ;;
    -*)        echo "unknown option: $arg" >&2; usage 1 ;;
    *)
      case " $ALL_TARGETS " in
        *" $arg "*) targets="$targets $arg" ;;
        *) die "unknown target '$arg' (use one of: $ALL_TARGETS, all)" ;;
      esac ;;
  esac
done
targets="${targets:-esp01_1m}"

PIO="$(command -v pio || command -v platformio || true)"
[ -n "$PIO" ] || die "PlatformIO not found. Install it: https://platformio.org/install/cli"

# The mesh key: a build without config.local.h uses the PUBLIC default key.
if [ ! -f "$LOCAL_CFG" ]; then
  warn "no $LOCAL_CFG, so this build uses the PUBLIC default mesh key (testing only)."
  warn "run ./tools/new-psk.sh to create your own key before flashing real nodes."
elif ! grep -q '^[[:space:]]*#define[[:space:]]\{1,\}MESH_PSK' "$LOCAL_CFG"; then
  warn "$LOCAL_CFG does not define MESH_PSK, the public default key will be used."
fi

if [ "$run_tests" = 1 ]; then
  echo "==> native tests"
  CXX="$(command -v g++ || command -v clang++ || true)"
  [ -n "$CXX" ] || die "no C++ compiler found for the native tests"
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT
  "$CXX" -std=c++17 -O1 -Wall -Wextra -Wshadow -Werror tests/native/test_logic.cpp -o "$tmp/bn_test"
  "$tmp/bn_test"
fi

if [ "$clean" = 1 ]; then
  echo "==> clean"
  "$PIO" run -t clean >/dev/null 2>&1 || true
  rm -rf dist
fi

VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo dev)"
mkdir -p dist

for t in $targets; do
  echo "==> build $t ($VERSION)"
  "$PIO" run -e "$t"
  src=".pio/build/$t/firmware.bin"
  [ -f "$src" ] || die "build finished but $src is missing"
  cp "$src" "dist/bunkernet-$t-$VERSION.bin"
done

# checksums for everything in dist/
(
  cd dist
  rm -f SHA256SUMS
  if command -v sha256sum >/dev/null 2>&1; then sha256sum ./*.bin; else shasum -a 256 ./*.bin; fi | sed 's# \./# #' > SHA256SUMS
)

echo
echo "Done. Binaries:"
for t in $targets; do
  f="dist/bunkernet-$t-$VERSION.bin"
  printf '  %s  (%s bytes)\n' "$f" "$(wc -c < "$f" | tr -d ' ')"
done
echo "Flash one with: ./tools/flash.sh <target>   (or --bin <file> to use a prebuilt binary)"
