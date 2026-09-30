#!/usr/bin/env bash
# Runs the launcher in a virtual X display, drives it with keys and takes
# screenshots.
#
#   shoot.sh <launcher binary> <sandbox dir> <output prefix> [step ...]
#
# Steps: key:<xdotool keys>   wait:<seconds>   snap:<name>
#        move:<x>,<y>         click:<button>
# Each snap is written to "<output prefix>-<name>.png"; the launcher's
# stdout / stderr go to "<output prefix>.log".
# Needs Xvfb, xdotool and ImageMagick (import).
set -u
BIN="$1"; SANDBOX="$2"; OUT="$3"; shift 3
DISP=":$((90 + RANDOM % 9))"
Xvfb "$DISP" -screen 0 1280x720x24 -nolisten tcp > /dev/null 2>&1 &
XPID=$!
sleep 1
export DISPLAY="$DISP"
export LIBGL_ALWAYS_SOFTWARE=1          # Mesa llvmpipe: works without a GPU
export SDL_AUDIODRIVER=dummy
(cd "$SANDBOX" && exec "$BIN") > "$OUT.log" 2>&1 &
APID=$!
sleep 1
WID=$(xdotool search --sync --onlyvisible --pid "$APID" 2>/dev/null | head -1)
[ -n "$WID" ] && xdotool windowactivate --sync "$WID" 2>/dev/null
for step in "$@"; do
  case "$step" in
    key:*)   xdotool key --window "$WID" --delay 60 ${step#key:}; sleep 0.25 ;;
    wait:*)  sleep "${step#wait:}" ;;
    snap:*)  import -window root "$OUT-${step#snap:}.png" ;;
    move:*)  xdotool mousemove $(echo "${step#move:}" | tr ',' ' '); sleep 0.2 ;;
    click:*) xdotool click "${step#click:}"; sleep 0.25 ;;
    *) echo "shoot.sh: unknown step '$step'" >&2 ;;
  esac
done
kill "$APID" 2>/dev/null; sleep 0.5; kill -9 "$APID" 2>/dev/null
kill "$XPID" 2>/dev/null
wait 2>/dev/null
exit 0
