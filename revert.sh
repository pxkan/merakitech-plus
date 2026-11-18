#!/usr/bin/env bash
# revert.sh — remove Meraki+ and put the display back to the stock UI. Safe to run repeatedly.
#
#   ./revert.sh --ip ADDR             stop Meraki+, start the stock UI, restore demo.sh + slot B + boot flag
#   ./revert.sh --ip ADDR --dry-run   show what would happen
#   ./revert.sh --ip ADDR --purge     also delete /new_customer/ourui (backups, language, settings backups)
#
# Slot A (the stock UI) is never modified by Meraki+, so the stock UI is always there to go back to.
set -euo pipefail
command -v python3 >/dev/null 2>&1 || { echo "revert.sh: python3 is required" >&2; exit 1; }
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "$#" -eq 0 ]; then echo "usage: ./revert.sh --ip ADDR [--dry-run] [--purge]"; exit 2; fi
case "${1:-}" in -h|--help) exec python3 "$HERE/tools/merakiplus.py" revert --help;; esac
python3 "$HERE/tools/merakiplus.py" revert "$@"
