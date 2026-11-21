#!/usr/bin/env bash
# Flash BunkerNet firmware to a module over a USB-serial adapter (macOS and Linux).
#
#   ./tools/flash.sh                       build and flash the default target (esp01_1m)
#   ./tools/flash.sh nodemcuv2             another target (esp01_1m, esp01_relay, nodemcuv2)
#   ./tools/flash.sh --bin dist/x.bin      flash a prebuilt binary (needs esptool, not PlatformIO)
#   ./tools/flash.sh -p /dev/ttyUSB0 -m    pick the port, then open the serial monitor
#   ./tools/flash.sh --erase               wipe the whole flash first (also wipes saved state)
#   ./tools/flash.sh --dry-run             show what would happen
#
# Options: -p/--port PORT  --bin FILE  --erase  -m/--monitor  --baud N  -y/--yes  --dry-run
#          --allow-default-key   (flash a build that uses the PUBLIC default mesh key)
# ESP-01: hold GPIO0 low (to GND) while powering up, release it after flashing. See the README.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ALL_TARGETS="esp01_1m esp01_relay nodemcuv2"
LOCAL_CFG="firmware/bunkernet/config.local.h"

die()  { echo "error: $*" >&2; exit 1; }
warn() { echo "warning: $*" >&2; }
usage() { sed -n '2,14p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

target="esp01_1m"; port=""; bin=""; erase=0; monitor=0; baud=""; yes=0; dry=0; allow_default=0
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage 0 ;;
    -p|--port) [ $# -ge 2 ] || die "$1 needs a value"; port="$2"; shift ;;
    --bin)     [ $# -ge 2 ] || die "--bin needs a file"; bin="$2"; shift ;;
    --baud)    [ $# -ge 2 ] || die "--baud needs a number"; baud="$2"; shift ;;
    --erase)   erase=1 ;;
    -m|--monitor) monitor=1 ;;
    -y|--yes)  yes=1 ;;
    --dry-run) dry=1 ;;
    --allow-default-key) allow_default=1 ;;
    -*)        echo "unknown option: $1" >&2; usage 1 ;;
    *)
      case " $ALL_TARGETS " in
        *" $1 "*) target="$1" ;;
        *) die "unknown target '$1' (use one of: $ALL_TARGETS)" ;;
      esac ;;
  esac
  shift
done
[ -z "$bin" ] || [ -f "$bin" ] || die "binary not found: $bin"
[ -n "$baud" ] || baud=115200

run() {                                   # print the command, run it unless --dry-run
  echo "+ $*"
  if [ "$dry" = 0 ]; then "$@"; fi
}

# ---------------------------------------------------------------- serial port
find_ports() {
  local p
  for p in /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.usbmodem* \
           /dev/ttyUSB* /dev/ttyACM*; do
    [ -e "$p" ] && echo "$p"
  done
  return 0
}

if [ -z "$port" ]; then
  ports="$(find_ports)"
  n=0; [ -z "$ports" ] || n="$(printf '%s\n' "$ports" | wc -l | tr -d ' ')"
  if [ "$n" = 0 ]; then
    if [ "$dry" = 1 ]; then port="<serial-port>"
    else die "no USB-serial adapter found. Plug it in (3.3 V!), or pass --port /dev/..."; fi
  elif [ "$n" = 1 ]; then
    port="$ports"
  else
    [ "$yes" = 0 ] || die "several serial ports found, choose one with --port:
$ports"
    echo "Several serial ports found:"
    i=1; printf '%s\n' "$ports" | while read -r p; do echo "  $i) $p"; i=$((i+1)); done
    read -r -p "Which one? [1-$n] " choice
    case "$choice" in ''|*[!0-9]*) die "invalid choice" ;; esac
    [ "$choice" -ge 1 ] && [ "$choice" -le "$n" ] || die "invalid choice"
    port="$(printf '%s\n' "$ports" | sed -n "${choice}p")"
  fi
fi

# ---------------------------------------------------------------- tools
PIO="$(command -v pio || command -v platformio || true)"

ESPTOOL=()                                # the esptool command, kept as an array so paths with spaces work
if command -v esptool.py >/dev/null 2>&1; then ESPTOOL=(esptool.py)
elif command -v esptool >/dev/null 2>&1; then ESPTOOL=(esptool)
elif command -v python3 >/dev/null 2>&1 && python3 -m esptool version >/dev/null 2>&1; then ESPTOOL=(python3 -m esptool)
elif [ -f "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" ] && command -v python3 >/dev/null 2>&1; then
  ESPTOOL=(python3 "$HOME/.platformio/packages/tool-esptoolpy/esptool.py")
fi
if [ -n "$bin" ] || [ "$erase" = 1 ]; then
  [ "${#ESPTOOL[@]}" -gt 0 ] || [ "$dry" = 1 ] || die "esptool not found. Install PlatformIO (it bundles esptool) or: pip install esptool"
fi
[ -n "$bin" ] || [ -n "$PIO" ] || [ "$dry" = 1 ] || die "PlatformIO not found (https://platformio.org/install/cli). Or flash a prebuilt file with --bin."

# ---------------------------------------------------------------- what is about to happen
key_state="your own key ($LOCAL_CFG)"
if [ -n "$bin" ]; then
  key_state="whatever key the binary was built with (release binaries use the PUBLIC default key)"
elif [ ! -f "$LOCAL_CFG" ]; then
  key_state="the PUBLIC default key: run ./tools/new-psk.sh first for real use"
  if [ "$allow_default" = 0 ] && [ "$dry" = 0 ]; then
    die "no $LOCAL_CFG: this would flash the PUBLIC default mesh key. Run ./tools/new-psk.sh, or pass --allow-default-key for a test node."
  fi
fi

echo "BunkerNet flash"
echo "  target : $target${bin:+  (prebuilt: $bin)}"
echo "  port   : $port at $baud baud"
echo "  key    : $key_state"
[ "$erase" = 0 ] || echo "  erase  : the whole flash, including saved messages and identities"

# Read the MAC so the module can be labelled: the node name is derived from it.
if [ "$dry" = 0 ] && [ "${#ESPTOOL[@]}" -gt 0 ]; then
  if mac_out="$("${ESPTOOL[@]}" --port "$port" --baud "$baud" read_mac 2>&1)"; then
    mac="$(printf '%s\n' "$mac_out" | grep -i -o 'MAC: *[0-9a-f:]\{17\}' | head -1 | sed 's/MAC: *//')"
    if [ -n "$mac" ]; then
      last2="$(printf '%s' "$mac" | awk -F: '{print toupper($5 $6)}')"
      echo "  module : MAC $mac, it will appear as Node-$last2 (write it on a label)"
    fi
  else
    echo "$mac_out" | tail -5 >&2
    die "could not talk to the module on $port. For an ESP-01: GPIO0 to GND while powering up, 3.3 V with 300 mA or more, TX/RX crossed."
  fi
fi

if [ "$yes" = 0 ] && [ "$dry" = 0 ]; then
  read -r -p "Flash now? [y/N] " ans
  case "$ans" in y|Y|yes|YES) ;; *) echo "cancelled"; exit 1 ;; esac
fi

# ---------------------------------------------------------------- erase, flash
if [ "$erase" = 1 ]; then
  run "${ESPTOOL[@]+"${ESPTOOL[@]}"}" --port "$port" --baud "$baud" erase_flash
  echo "Flash erased. Power-cycle the module with GPIO0 still low before flashing (ESP-01)."
  if [ "$dry" = 0 ] && [ "$yes" = 0 ]; then read -r -p "Press Enter when ready to flash... " _; fi
fi

if [ -n "$bin" ]; then
  run "${ESPTOOL[@]+"${ESPTOOL[@]}"}" --port "$port" --baud "$baud" write_flash 0x0 "$bin"
else
  run "$PIO" run -e "$target" -t upload --upload-port "$port"
fi

echo
echo "Flashed."
if [ "$target" != "nodemcuv2" ]; then
  echo "ESP-01: release GPIO0 and power-cycle the module. It should start the Wi-Fi network after a few seconds."
else
  echo "The board restarts by itself."
fi

if [ "$monitor" = 1 ]; then
  if [ -n "$PIO" ]; then
    echo "Serial monitor (Ctrl+C to leave):"
    run "$PIO" device monitor -p "$port" -b 115200
  else
    warn "PlatformIO not found, open the serial monitor yourself at 115200 baud."
  fi
fi
