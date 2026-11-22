#!/bin/sh
# sim/studio.sh — Sim Studio: the real Meraki+ UI (WebAssembly) + a simulated machine, in the browser.
#
#   sim/studio.sh [--host H] [--port P] [--tailnet] [--no-classic]   build into build/studio and serve it
#                                                                    (default http://127.0.0.1:8765/)
#   sim/studio.sh build [--no-classic] [--classic-ref REF]           only build (build/studio)
#   sim/studio.sh deploy                                             build into $STUDIO_HOME/site (atomic swap) and
#                                                                    restart the LaunchAgent if it is installed
#   sim/studio.sh install-agent | uninstall-agent | status           the per-user LaunchAgent (macOS) that serves the
#                                                                    deployed site on the tailnet address, port 8765
#
# STUDIO_HOME defaults to ~/merakitech-plus-studio (site/ + serve.py live there, so the agent does not depend on a checkout).
# Needs zig 0.17.1 + git to build, python3 to serve. Never touches a device. See sim/README.md "Sim Studio".
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$HERE")
STUDIO_HOME=${STUDIO_HOME:-$HOME/merakitech-plus-studio}
LABEL=com.kenny.meraki-studio
PLIST=$HOME/Library/LaunchAgents/$LABEL.plist
PORT=${STUDIO_PORT:-8765}
PY=$(command -v python3)

deploy() {
  mkdir -p "$STUDIO_HOME"
  rm -rf "$STUDIO_HOME/site.new"
  "$PY" "$HERE/studio/build.py" --out "$STUDIO_HOME/site.new" "$@"
  cp "$HERE/studio/serve.py" "$STUDIO_HOME/serve.py"
  rm -rf "$STUDIO_HOME/site.old"
  [ -d "$STUDIO_HOME/site" ] && mv "$STUDIO_HOME/site" "$STUDIO_HOME/site.old"
  mv "$STUDIO_HOME/site.new" "$STUDIO_HOME/site"
  rm -rf "$STUDIO_HOME/site.old"
  echo "deployed -> $STUDIO_HOME/site"
  if launchctl print "gui/$(id -u)/$LABEL" >/dev/null 2>&1; then
    launchctl kickstart -k "gui/$(id -u)/$LABEL" && echo "restarted $LABEL"
  fi
}

case "${1:-}" in
  build) shift; exec "$PY" "$HERE/studio/build.py" "$@" ;;
  deploy) shift; deploy "$@" ;;
  install-agent)
    [ -d "$STUDIO_HOME/site" ] || deploy
    mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
    cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$PY</string><string>$STUDIO_HOME/serve.py</string>
    <string>--dir</string><string>$STUDIO_HOME/site</string>
    <string>--tailnet</string><string>--port</string><string>$PORT</string>
  </array>
  <key>EnvironmentVariables</key>
  <dict><key>PATH</key><string>/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin</string></dict>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>10</integer>
  <key>StandardOutPath</key><string>$HOME/Library/Logs/meraki-studio.log</string>
  <key>StandardErrorPath</key><string>$HOME/Library/Logs/meraki-studio.log</string>
</dict>
</plist>
EOF
    launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
    launchctl bootstrap "gui/$(id -u)" "$PLIST"
    echo "installed $LABEL ($PLIST) -> http://$(tailscale ip -4 2>/dev/null || echo '<tailnet-ip>'):$PORT/" ;;
  uninstall-agent)
    launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
    rm -f "$PLIST"; echo "removed $LABEL (the deployed site in $STUDIO_HOME is kept)" ;;
  status)
    launchctl print "gui/$(id -u)/$LABEL" 2>/dev/null | grep -E 'state =|pid =|last exit' || echo "$LABEL not loaded"
    tail -n 3 "$HOME/Library/Logs/meraki-studio.log" 2>/dev/null || true ;;
  -h|--help|help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//' ;;
  *)
    classic=
    for a in "$@"; do [ "$a" = --no-classic ] && classic=--no-classic; done
    "$PY" "$HERE/studio/build.py" --out "$ROOT/build/studio" $classic
    exec "$PY" "$HERE/studio/serve.py" --dir "$ROOT/build/studio" "$@" ;;
esac
