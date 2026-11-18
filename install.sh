#!/usr/bin/env bash
# install.sh — install or update Meraki+ on a MerakiTech Gen1/Gen2 display.
#
#   ./install.sh --ip 192.168.1.50            install into slot B and start it now (no reboot)
#   ./install.sh --scan                       find the display on your network first
#   ./install.sh --ip ADDR --dry-run          show every step, change nothing
#   ./install.sh --ip ADDR --persist          also start Meraki+ at every boot
#   ./install.sh --ip ADDR --gesture          also install the center-hold gesture (stock -> Meraki+)
#   ./install.sh --help                       all options
#
# Undo everything: ./revert.sh --ip ADDR
# READ THE DISCLAIMER IN README.md FIRST. You use this entirely at your own risk.
set -euo pipefail
REF="${MERAKIPLUS_REF:-main}"
RAW="https://raw.githubusercontent.com/thomasgrastein/merakitech-plus/${REF}"

if ! command -v python3 >/dev/null 2>&1; then
  echo "install.sh: python3 is required (macOS: xcode-select --install, Debian/Ubuntu: apt install python3)" >&2
  exit 1
fi
python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)' || {
  echo "install.sh: python3 >= 3.8 is required" >&2; exit 1; }

SRC="${BASH_SOURCE[0]:-}"
if [ -n "$SRC" ] && [ -f "$SRC" ] && [ -f "$(dirname "$SRC")/tools/merakiplus.py" ]; then
  TOOL="$(cd "$(dirname "$SRC")" && pwd)/tools/merakiplus.py"
else
  # Standalone mode (e.g. piped from curl). The binaries are still md5-checked, but this
  # helper script itself is not — prefer `git clone` (see README).
  TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
  echo "install.sh: standalone mode, fetching the installer from ${RAW}/tools/merakiplus.py"
  if command -v curl >/dev/null 2>&1; then curl -fsSL "${RAW}/tools/merakiplus.py" -o "$TMP/merakiplus.py"
  else python3 -c 'import sys,urllib.request; open(sys.argv[2],"wb").write(urllib.request.urlopen(sys.argv[1],timeout=60).read())' \
         "${RAW}/tools/merakiplus.py" "$TMP/merakiplus.py"; fi
  TOOL="$TMP/merakiplus.py"
  set -- "$@" --ref "$REF"
fi

if [ "$#" -eq 0 ]; then
  echo "usage: ./install.sh --ip ADDR [--persist] [--gesture] [--dry-run]   (or --scan)"
  echo "       ./install.sh --help"
  exit 2
fi
case "${1:-}" in -h|--help) exec python3 "$TOOL" install --help;; esac
python3 "$TOOL" install "$@"
