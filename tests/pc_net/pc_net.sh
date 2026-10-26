#!/bin/bash
# PC <-> PC network game on localhost (ROADMAP 12.14): a host and a joiner, headless (SDL dummy drivers), real time (50 Hz), plain --keys input
# through the Multiplayer screen and the lobby, then the lockstep; afterwards the `net: hash tick N crc X` lines of the two logs are compared.
#
#   tests/pc_net/pc_net.sh <outdir> [coop|classic] [frames] [post-start keys of the host | fight] [post-start keys of the joiner]
#   MS_FILM=N: a PNG of each instance every N frames in <outdir>/{h,j}/film;  MS_PORT, MS_EXE, MS_DISKS as in tests/pc_play/pc_play.sh
#
# The post-start keys are --keys text (docs/PC_PORT.md): "@N" waits N frames, "+Key" / "-Key" press and release.  The host plays with the
# arrow keys + Right_Ctrl / Space (joystick 1), the joiner picks Keys WASD in the lobby: W A S D + LAlt / E.
# Needs build-sdl/moonstone.exe (MS_EXE) and the disks (MS_DISKS).  Each instance gets its own copy of the exe (the log is written next to it),
# its own --pref directory and a net.ini with `public = no`: nothing leaves the machine.  The menu timing is a script, not a barrier: a run
# whose lockstep did not begin is repeated (up to 3 tries).  Exit 0 = lockstep began and every common hash tick matched.

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
EXE=${MS_EXE:-$ROOT/build-sdl/moonstone.exe}
if [ -z "$MS_DISKS" ]; then
  MS_DISKS=$ROOT/disks
  [ -d "$MS_DISKS" ] || MS_DISKS=$(dirname "$(git -C "$ROOT" rev-parse --git-common-dir)")/moonstone-ace/disks
fi
[ $# -ge 1 ] || { sed -n 2,12p "$0" >&2; exit 2; }
OUT=$1; MODE=${2:-coop}; FRAMES=${3:-2500}; HX=${4:-}; JX=${5:-}
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
# the named scenario "fight" (both instances): walk, end the turn, dismiss "Next Day", enter the lair (key 2), mash the joystick patterns of
# src/rt/autoplay.cpp (the mashs of tests/pc_play/pc_play.sh) through the arena fight; in coop each instance steers its own knight
MASHPATS=("Space" "Left Space" "Right Space" "Up Space" "Down Space" "Left Up Space" "Right Up Space" "Left Down Space" "Right Down Space" "Left" "Right" "Up" "Down" "")
mashs() { local n=$1 hold=${2:-8} seed=${3:-3} mul=${4:-7} s="" idx=0 k i; for ((i=0;i<n;i++)); do idx=$(( (i*mul+seed) % 14 )); for k in ${MASHPATS[$idx]}; do s="$s +$k"; done; s="$s @$hold"; for k in ${MASHPATS[$idx]}; do s="$s -$k"; done; s="$s @2"; done; echo "$s"; }
# "fight": both instances mash; "fighth": only the host mashes (the joiner's knight stands still and falls, the host wins, loot, next day): a network
# "fightj": only the joiner mashes (its Space presses used to start the fight pause that nobody could end: both machines hung; now ignored in net games).
# co-op fight where one knight dies while the other fights on (regression: a flyer's finishing blow marked the OTHER knight dead and ended the fight)
case "$HX" in fight|fighth|fightj)
  WHICH=$HX
  FP="@100 +Right @40 -Right @10 +Down @20 -Down @50 E @900 +Right_Ctrl @8 -Right_Ctrl @400 +Right_Ctrl @8 -Right_Ctrl @150 2 @300"
  MASH="$(mashs 150 8 10 7) @300"
  HX="$FP $MASH"; JX="$FP $MASH"
  # the fallen knight's sheet is closed by its owner (cursor to the pillar, fire), then the leader's loot screen: gold taken, leave, "Next Day" dismissed
  CLOSE="+Right +Down @200 -Right -Down @5 +Left @37 -Left @3 +Up @23 -Up @10 +Right_Ctrl @10 -Right_Ctrl @200"
  LOOT="@400 $CLOSE +Up @40 -Up @10 +Right @40 -Right @5 +Down @30 -Down @10 +Right_Ctrl @10 -Right_Ctrl @100 +Left @40 -Left @5 +Down @10 -Down @10 +Right_Ctrl @10 -Right_Ctrl @300"
  NEXTDAY="@600 +Right_Ctrl @8 -Right_Ctrl @300"
  [ "$WHICH" = fighth ] && JX="$FP @1500 $LOOT $NEXTDAY" && HX="$FP $MASH $LOOT $NEXTDAY"
  [ "$WHICH" = fightj ] && HX="$FP @1500 $LOOT $NEXTDAY" && JX="$FP $MASH $LOOT $NEXTDAY"
  ;;
esac
PORT=${MS_PORT:-6502}
FILMH=(); FILMJ=()   # MS_FILM=N: a PNG of each instance every N frames in <outdir>/{h,j}/film
if [ -n "$MS_FILM" ]; then mkdir -p "$OUT/h/film" "$OUT/j/film"; FILMH=(--dump-every "$MS_FILM" "$OUT/h/film"); FILMJ=(--dump-every "$MS_FILM" "$OUT/j/film"); fi
export SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy
export MS_NET_BIND=${MS_NET_BIND:-loopback}   # both instances talk over localhost: no Windows Firewall prompt per exe copy (src/rt/sdl/winsock.cpp)

# title menu: Down = Multiplayer, fire; the Multiplayer screen
OPEN="@180 +Down @8 -Down @40 +Space @8 -Space @150"
# the host: HOST GAME, the lobby (the mode line first for classic: up, left, down; presses of 5 frames are read once), portrait, name, controller, START
MODEKEYS=""
[ "$MODE" = classic ] && MODEKEYS="+Up @5 -Up @100 +Left @5 -Left @100 +Down @5 -Down @100"
HK="$OPEN +Space @8 -Space @330 $MODEKEYS +Space @20 -Space @70 +Space @20 -Space @70 +Space @20 -Space @500 +Down @12 -Down @70 +Space @20 -Space @300 $HX"
# the joiner: JOIN GAME, the join page (address from --join), down twice to CONNECT, the lobby, portrait, name, controller Keys WASD
JK="$OPEN +Down @8 -Down @30 +Space @8 -Space @100 +Down @8 -Down @30 +Down @8 -Down @30 +Space @8 -Space @250 +Space @20 -Space @70 +Space @20 -Space @70 +Left @12 -Left @70 +Space @20 -Space @300 $JX"

for d in h j; do
  mkdir -p "$OUT/$d/pref"
  cp "$EXE" "$(dirname "$EXE")/SDL3.dll" "$OUT/$d/"
  printf '[net]\npublic = no\n' > "$OUT/$d/pref/net.ini"
done
for try in 1 2 3; do
  rm -f "$OUT/h/moonstone.log" "$OUT/j/moonstone.log"
  (cd "$OUT/h" && ./moonstone.exe --disks "$MS_DISKS" --pref "$OUT/h/pref" --skip-intro --no-grab --frames "$FRAMES" --host --port "$PORT" --name HOSTY \
      --dump-frame "$OUT/h/end.png" "${FILMH[@]}" --keys "$HK" > out.txt 2>&1) &
  sleep 3
  (cd "$OUT/j" && ./moonstone.exe --disks "$MS_DISKS" --pref "$OUT/j/pref" --skip-intro --no-grab --frames "$((FRAMES - 100))" --join 127.0.0.1 --port "$PORT" --name GUESTY \
      --dump-frame "$OUT/j/end.png" "${FILMJ[@]}" --keys "$JK" > out.txt 2>&1) &
  wait
  if grep -q "lockstep begins" "$OUT/h/moonstone.log" 2>/dev/null && grep -q "lockstep begins" "$OUT/j/moonstone.log" 2>/dev/null; then break; fi
  echo "try $try: the lockstep did not begin (menu timing), again" >&2
done
py "$ROOT/tests/pc_net/hashcmp.py" "$OUT/h/moonstone.log" "$OUT/j/moonstone.log"
