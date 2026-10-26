#!/bin/bash
# PC play scripts: plain --keys input (no debug hooks, no pokes) that plays the PC build from the title menu to the map, a lair fight,
# the loot screen, the end of the turn, a town with its shops, the wizard, the Valley of the Gods and Stonehenge.
#
#   tests/pc_play/pc_play.sh <segment> <outdir> <frame> [<frame> ...]
#
# runs build-sdl/moonstone.exe headless (--fast, SDL dummy drivers) once per frame number with the key script of <segment> and writes
# <outdir>/s<frame>.png (the 320x200 frame at that frame number).  Segments: map lair arena fight town shop wizard valley stonehenge, and the
# local co-op ones coopmap coopbk (the black-knight duel: --warp-knight, the arena ~3000) coopfight cooploot cooptown coopswitch coopshop coopsheet coop4 coop4lair, and the computer-knight ones coopaiknight coopai
# coopaifight coopailoot coopaisheet coopaishop coopaishopsheet coop4ai coop4ailair (see below).
# Environment: MS_EXE (default build-sdl/moonstone.exe), MS_DISKS (the drawer with the three Moonstone ADFs; default <repo>/disks, else the
# main checkout's disks/), MS_AUDIO (a .wav path: --dump-audio of the last run), MS_ARGS (more exe options).
#
# The random seed is the Amiga's (LAB_0974 read as big-endian longs, beam index 3 in --fast mode: 0000ACFB), so the walks of the black knights
# are the original's for this input; a route that works on one seed index need not on another (real-time runs pick the index by the beam).
# The game is deterministic for a fixed key script, so the frame numbers below are exact for the build they were found with; a change that
# alters timing (or the random seed's use) moves them.  The fight uses a fixed "mash" of the joystick patterns of src/rt/autoplay.cpp
# (mashs N hold seed mul); the seed (10, 7, 235 patterns) was found by running a grid and looking for the loot screen.
#
# Controls on the PC (--joykeys arrows): cursor keys = joystick 1, Right_Ctrl and Space = fire (Space on the map opens the status sheet, the
# fight mash uses it as fire), E ends the turn on the map, 1..9 pick a row of the arrival menu.  The cursor of the screens (loot, shops)
# moves 2 px per frame while a direction is held, a click needs fire held ~8 frames.
#
# Local co-op (ROADMAP M8) on one keyboard, segments coop* below: knight 1 = Joystick 1 (cursor keys, Right_Ctrl / Space fire), knight 2 = "Keys WASD"
# (W A S D, Left_Alt fire; the knight screen offers it by itself on a PC without a second gamepad, rt/sdl/input.cpp padUsable), knight 3 / 4 = Joystick 2 /
# Keys arrows need gamepads (docs/PC_PORT.md "Local co-op").  The party is ONE token, the leader rotates each round: round 1 knight 1 walks (arrows),
# round 2 knight 2 (WASD).  Fires on the "Next Day" screen take any controller.
#
# Frames of the checkpoints (this build): title menu ~150, map ~1700, arrival menu (home village + lair) ~3100, lair arena ~3500, loot screen
# ~6050, back on the map ~6650; town A menu ~2400, smith ~2600, dice ~2800 after TOWNA; wizard tower ~6000 (WIZARD), valley refusal ~3700, Stonehenge offer ~3500.
# Co-op checkpoints: Select Knight ~700 (knight 2's control line "Keys WASD" ~2500), party on the map ~2850, "Next Day" ~3750, round 2 (knight 2 leads) ~4000,
# coopsheet: knight 2's sheet ~4300, knight 1's ~4900; coopfight: arena ~4200, fight ~5000, knight 2 down ~5200; cooploot: loot screen ~5420, map ~5700, "Next Day" ~6400;
# cooptown: "SIR GODBER'S TURN" banner ~3230, corner "< NAME >" ~3300; coopswitch: knight 2's town ~3700; coopshop: its smith ~3950; coop4: map ~4800; coop4lair: arena ~5000, fight ~5900.
# Computer knights (ROADMAP 8.3b; frames of this build): coopaiknight: knight 2's controller line reads "Computer" ~2500, map ~2850; coopai: lair arena ~4500;
# coopaifight: human + computer knight both fight ~4600-5700, loot screen ~6150 (cleared, both alive); coopailoot: gold taken (both sheets +27) ~6500, "Next Day" ~7300;
# coopaisheet: knight 1's sheet ~7900, the computer knight's ~8500 (closed with knight 1's fire); coopaishop: the smith ~3950 (broad sword, 10 gp);
# coopaishopsheet: both sheets show the broad sword and 0 gp, knight 1's ~6500, the computer knight's ~6700; coop4ai: knight 2's line ~2500, map ~4800;
# coop4ailair: arrival menu ~5800, one human + three computer knights in the arena ~6300-7800.  The mash of coopaifight (130 patterns, hold 8, seed 3, mul 7) was found by
# a scan: a different mash loses the fight (the partner is not a second human).  Random-key soak of the same set-up: tests/pc_play/coop_soak.py.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
EXE=${MS_EXE:-$ROOT/build-sdl/moonstone.exe}
if [ -z "$MS_DISKS" ]; then
  MS_DISKS=$ROOT/disks
  [ -d "$MS_DISKS" ] || MS_DISKS=$(dirname "$(git -C "$ROOT" rev-parse --git-common-dir)")/moonstone-ace/disks
fi

# ---- key script pieces ------------------------------------------------------------------------------------------------
# title -> Local -> Select Knight (3 x down) -> first knight -> keep the name -> the map (frame ~1650)
# frames of idle after the knight select, before the first walk: the random events of the next turns depend on it (MS_LEAD overrides, for scans)
LEAD=${MS_LEAD:-10}
PRE="@199 +Space @8 -Space @52 +Down @10 -Down @20 +Down @10 -Down @20 +Down @10 -Down @60 +Space @8 -Space @492 +Space @8 -Space @292 +Space @8 -Space @400"
# end the turn at once, wait for the black knights' moves, dismiss "Next Day" with fire, wait for the map
ENDTURN="E @750 +Right_Ctrl @8 -Right_Ctrl @300"

MASHPATS=("Space" "Left Space" "Right Space" "Up Space" "Down Space" "Left Up Space" "Right Up Space" "Left Down Space" "Right Down Space" "Left" "Right" "Up" "Down" "")
# mashs N hold seed mul: N patterns of the table above (index (i*mul+seed) mod 14), each held `hold` frames
mashs() { local n=$1 hold=${2:-8} seed=${3:-3} mul=${4:-7} s="" idx=0 k i; for ((i=0;i<n;i++)); do idx=$(( (i*mul+seed) % 14 )); for k in ${MASHPATS[$idx]}; do s="$s +$k"; done; s="$s @$hold"; for k in ${MASHPATS[$idx]}; do s="$s -$k"; done; s="$s @2"; done; echo "$s"; }

# the arrival menu on the home village + lair (frame ~3100): move off the start node, end turn 1, fire twice
MENU="$PRE @$LEAD +Right @40 -Right @10 +Down @20 -Down @50 E @900 +Right_Ctrl @8 -Right_Ctrl @400 +Right_Ctrl @8 -Right_Ctrl @150"
# key 2 = Enter lair: the arena (a "be" lair, six beasts) at frame ~3500
ARENA="$MENU 2 @300"
# the fight: a fixed mash that wins, then the loot screen: take the gold (cursor to the chest, fire), leave by the middle pillar
FIGHT="$ARENA $(mashs 235 8 10 7) @400 +Up @40 -Up @10 +Right @40 -Right @5 +Down @30 -Down @10 +Right_Ctrl @10 -Right_Ctrl @100 +Left @40 -Left @5 +Down @10 -Down @10 +Right_Ctrl @10 -Right_Ctrl @300"
# town A (node 4) in turn 1: east, south, east, fire (the menu: Visit Merchant Tavern Healer High Temple Exit)
TOWN="$PRE @$LEAD +Right @85 -Right +Down @25 -Down +Right @60 -Right @20 +Right_Ctrl @8 -Right_Ctrl @300"
# the merchant: up to Merchant, the smith, the broad sword (10 gp, all the knight has), leave by the pillar, back to the town menu
SHOP="$TOWN +Up @32 -Up @30 +Right_Ctrl @8 -Right_Ctrl @250 +Left @20 -Left +Down @36 -Down @20 +Right_Ctrl @10 -Right_Ctrl @150 +Right @30 -Right +Down @18 -Down @20 +Right_Ctrl @10 -Right_Ctrl @400"
# the wizard's tower (node 8): three turns along the top edge (the first up against it: a black knight meets the knight lower down
# with the Amiga's random seed, see the RNG note above), fire; the next fires walk through his speech
WIZARD="$PRE @$LEAD +Right +Up @240 -Right -Up @10 $ENDTURN +Right @240 -Right @10 $ENDTURN +Right +Down @120 -Right -Down @10 $ENDTURN @20 +Right_Ctrl @8 -Right_Ctrl @500 +Right_Ctrl @8 -Right_Ctrl @300 +Right_Ctrl @8 -Right_Ctrl @300 +Right_Ctrl @8 -Right_Ctrl @300"
# the Valley of the Gods (node 6): refused without the four keys
VALLEY="$PRE @$LEAD +Right +Down @240 -Right -Down @10 $ENDTURN +Right @100 -Right +Up @15 -Up @20 +Right @90 -Right @20 +Right +Up @30 -Right -Up @30 +Right_Ctrl @8 -Right_Ctrl @400"
# Stonehenge (node 7): the offer text ("an item of magical nature to Danu"); with an item (the lair's talisman) fire goes on to the ritual
STONEHENGE="$PRE @$LEAD +Down +Right @100 -Down -Right +Down @140 -Down @10 $ENDTURN +Down +Right @150 -Down -Right @20 +Down @40 -Down @20 +Right_Ctrl @8 -Right_Ctrl @300"

# ---- local co-op ------------------------------------------------------------------------------------------------------
# title -> Local -> Players (Right: 1) -> Mode (Right: Coop, which makes it 2 players) -> Select Knight; two knights: picture, name, controller line each
# (knight 2 is offered Keys WASD), then the map at frame ~2830
S="+Space @8 -Space"
CO_PRE="@199 $S @60 +Right @8 -Right @20 +Down @8 -Down @20 +Right @8 -Right @30 +Down @8 -Down @20 +Down @8 -Down @20 $S @500"
CO2="$CO_PRE $S @300 $S @300 $S @300 $S @300 $S @300 $S @300"
# round 1: knight 1 walks right (arrows), E ends the turn, the black knights move, "Next Day" (frame ~3750) -> fire -> round 2: knight 2 leads (WASD)
CO_ROUND="E @750 +Right_Ctrl @8 -Right_Ctrl @300"
COOPMAP="$CO2 @10 +Right @40 -Right @30 $CO_ROUND +D @40 -D @30"
# a sheet's owner closes it with its own fire after moving the cursor onto the pillar (cursor to the bottom right corner first)
CO_CLOSE_J1="+Right +Down @200 -Right -Down @5 +Left @37 -Left @3 +Up @23 -Up @10 +Right_Ctrl @10 -Right_Ctrl @200"
CO_CLOSE_WASD="+D +S @200 -D -S @5 +A @37 -A @3 +W @23 -W @10 +Left_Alt @10 -Left_Alt @200"
# round 2 on the home village + lair: knight 1 walks onto the node, ends the turn; knight 2 (leader) enters the lair with its fire: the arena at ~4180
CO_LAIR="$CO2 @10 +Right @40 -Right @10 +Down @20 -Down @50 $CO_ROUND +Left_Alt @8 -Left_Alt @150"
# the two players mash their pads (arrows + Space, WASD + Left_Alt): mash2 N hold seed mul
CO_P1=("Space" "Left Space" "Right Space" "Up Space" "Down Space" "Left Up Space" "Right Up Space" "Left Down Space" "Right Down Space" "Left" "Right" "Up" "Down" "")
CO_P2=("Left_Alt" "A Left_Alt" "D Left_Alt" "W Left_Alt" "S Left_Alt" "A W Left_Alt" "D W Left_Alt" "A S Left_Alt" "D S Left_Alt" "A" "D" "W" "S" "")
mash2() { local n=$1 hold=${2:-8} seed=${3:-3} mul=${4:-7} s="" i k i1 i2; for ((i=0;i<n;i++)); do i1=$(( (i*mul+seed) % 14 )); i2=$(( (i*(mul+2)+seed+5) % 14 )); for k in ${CO_P1[$i1]}; do s="$s +$k"; done; for k in ${CO_P2[$i2]}; do s="$s +$k"; done; s="$s @$hold"; for k in ${CO_P1[$i1]}; do s="$s -$k"; done; for k in ${CO_P2[$i2]}; do s="$s -$k"; done; s="$s @2"; done; echo "$s"; }
# this mash (128 patterns, hold 6, seed 5, mul 7; 110 patterns lose) wins with the Amiga's seed: knight 2 falls, knight 1 clears the lair, arena at ~4180, the fight ends in the idle time after the mash (~5300), loot screen from ~5400
COOPFIGHT="$CO_LAIR $(mash2 128 6 5 7)"
# the loot screen belongs to the leader (knight 2): cursor onto the pile (WASD), take it, leave by the middle pillar, "Next Day" at ~6550, round 3
COOPLOOT="$COOPFIGHT @200 +D @40 -D @5 +S @8 -S @10 +Left_Alt @10 -Left_Alt @100 +A @40 -A @5 +S @10 -S @10 +Left_Alt @10 -Left_Alt @300 @300 +Right_Ctrl @8 -Right_Ctrl @300"
# town A in round 1 (as TOWN), then the corner arrow ">" (cursor to the box: 2 px per frame) hands the screens to knight 2
COOPTOWN="$CO2 @10 +Right @85 -Right +Down @25 -Down +Right @60 -Right @20 +Right_Ctrl @8 -Right_Ctrl @300"
COOPSWITCH="$COOPTOWN +Left +Up @50 -Up @36 -Left @2 +Right @3 -Right +Down @4 -Down @10 +Right_Ctrl @10 -Right_Ctrl @150"
# knight 2's own gold: its smith (W to Merchant, WASD only; the arrows do nothing now)
COOPSHOP="$COOPSWITCH +Left @30 -Left @10 +W @32 -W @30 +Left_Alt @8 -Left_Alt @250"
# four knights (Players 4 in Coop; all four controllers on one keyboard are not possible, the extra two are only checked to boot): map at ~4800
CO4="@199 $S @60 +Down @8 -Down @20 +Right @8 -Right @30 +Up @8 -Up @20 +Right @8 -Right @20 +Right @8 -Right @30 +Down @8 -Down @20 +Down @8 -Down @20 +Down @8 -Down @20 $S @500 $S @300 $S @300 $S @300 $S @300 $S @300 $S @300 $S @300 $S @300 $S @300 $S @300 $S @300 $S @300"
# round 1 (E at once), round 2 knight 2 enters the lair: four knights, ratmen wave x4 at ~5700
CO4LAIR="$CO4 @10 +Right @40 -Right @10 +Down @20 -Down @50 E @300 +Right_Ctrl @8 -Right_Ctrl @400 +Left_Alt @8 -Left_Alt @150 $(mash2 60 8 5 7)"

# ---- computer knights (ROADMAP 8.3b): one human (knight 1, arrows + Right_Ctrl / Space) and knights 2..4 set to "Computer" on the knight screen -----------
# Knight 2's controller line offers Keys WASD first (a PC without a second gamepad); one Right on it reaches "Computer" (the last stop of the ring).  The
# human walks the party EVERY round (a computer knight never leads), so round 2 is knight 1's again, with the arrows.
CO_AI2="$CO_PRE $S @300 $S @300 $S @300 $S @300 $S @300 +Right @8 -Right @30 $S @300"
# round 1: knight 1 walks onto the home village + lair node and ends the turn, round 2 (knight 1 again): fire opens the arrival menu, 2 = Enter lair: arena ~4500
CO_AI_LAIR="$CO_AI2 @10 +Right @40 -Right @10 +Down @20 -Down @50 $CO_ROUND +Right_Ctrl @8 -Right_Ctrl @150"
# the human mashes (130 patterns, hold 8, seed 3, mul 7: the computer knight fights beside it, both live, the lair is cleared; loot screen ~6150); the loot
# screen is knight 1's: cursor onto the pile, take it (the computer knight gets a copy), leave by the middle pillar; "Next Day" ~7300, fire, round 3
CO_AI_FIGHT="$CO_AI_LAIR 2 @300 $(mashs 130 8 3 7) @500"
CO_AI_LOOT="$CO_AI_FIGHT +Up @40 -Up @10 +Right @40 -Right @5 +Down @30 -Down @10 +Right_Ctrl @10 -Right_Ctrl @100 +Left @40 -Left @5 +Down @10 -Down @10 +Right_Ctrl @10 -Right_Ctrl @300"
CO_AI_DAY="$CO_AI_LOOT @500 +Right_Ctrl @8 -Right_Ctrl @400"
# Space on the map: knight 1's sheet (~7900), closed with fire on the pillar, then the computer knight's sheet (~8500), closed with knight 1's fire too
CO_AI_SHEETS="$CO_AI_DAY +Space @8 -Space @300 $CO_CLOSE_J1 $CO_CLOSE_J1"
# town A in round 1, the smith: the broad sword (10 gp, all either knight has); the computer knight buys the same with its own 10 gp.  Leave the shop and the town:
# the turn is spent ("Next Day" ~5300), round 2: both sheets show the broad sword and 0 gp (knight 1's ~5900, the computer knight's ~6100)
CO_AI_TOWN="$CO_AI2 @10 +Right @85 -Right +Down @25 -Down +Right @60 -Right @20 +Right_Ctrl @8 -Right_Ctrl @300"
CO_AI_SHOP="$CO_AI_TOWN +Up @32 -Up @30 +Right_Ctrl @8 -Right_Ctrl @250 +Left @20 -Left +Down @36 -Down @20 +Right_Ctrl @10 -Right_Ctrl @150"
CO_AI_LEAVE="$CO_AI_SHOP +Right @30 -Right +Down @18 -Down @20 +Right_Ctrl @10 -Right_Ctrl @400 +Right +Down @250 -Right -Down @10 +Right_Ctrl @10 -Right_Ctrl @300"
CO_AI_SHOP_SHEETS="$CO_AI_LEAVE @700 +Right_Ctrl @8 -Right_Ctrl @400 +Space @8 -Space @300 $CO_CLOSE_J1 $CO_CLOSE_J1"
# four knights, one human + three computers (a computer knight each: one Right on each controller line, fires 6, 9 and 12): map ~4800, lair arena ~5000
CO4_AI="@199 $S @60 +Down @8 -Down @20 +Right @8 -Right @30 +Up @8 -Up @20 +Right @8 -Right @20 +Right @8 -Right @30 +Down @8 -Down @20 +Down @8 -Down @20 +Down @8 -Down @20 $S @500 $S @300 $S @300 $S @300 $S @300 $S @300 +Right @8 -Right @30 $S @300 $S @300 $S @300 +Right @8 -Right @30 $S @300 $S @300 $S @300 +Right @8 -Right @30 $S @300"
CO4_AI_LAIR="$CO4_AI @10 +Right @40 -Right @10 +Down @20 -Down @50 E @300 +Right_Ctrl @8 -Right_Ctrl @400 +Right_Ctrl @8 -Right_Ctrl @150 2 @300 $(mashs 60 8 5 7)"

keys_of() {
  case "$1" in
    map) echo "$PRE @$LEAD" ;;
    lair) echo "$MENU" ;;
    arena) echo "$ARENA" ;;
    fight) echo "$FIGHT" ;;
    town) echo "$TOWN" ;;
    shop) echo "$SHOP" ;;
    wizard) echo "$WIZARD" ;;
    valley) echo "$VALLEY" ;;
    stonehenge) echo "$STONEHENGE" ;;
    coopmap) echo "$COOPMAP" ;;
    coopbk) echo "$COOPMAP" ;;
    coopfight) echo "$COOPFIGHT" ;;
    cooploot) echo "$COOPLOOT" ;;
    cooptown) echo "$COOPTOWN" ;;
    coopswitch) echo "$COOPSWITCH" ;;
    coopshop) echo "$COOPSHOP" ;;
    coopsheet) echo "$COOPMAP +Space @8 -Space @300 $CO_CLOSE_WASD $CO_CLOSE_J1" ;;
    coop4) echo "$CO4" ;;
    coop4lair) echo "$CO4LAIR" ;;
    coopaiknight) echo "$CO_AI2" ;;
    coopai) echo "$CO_AI_LAIR 2 @300" ;;
    coopaifight) echo "$CO_AI_FIGHT" ;;
    coopailoot) echo "$CO_AI_LOOT" ;;
    coopaisheet) echo "$CO_AI_SHEETS" ;;
    coopaishop) echo "$CO_AI_SHOP" ;;
    coopaishopsheet) echo "$CO_AI_SHOP_SHEETS" ;;
    coop4ai) echo "$CO4_AI" ;;
    coop4ailair) echo "$CO4_AI_LAIR" ;;
    *) echo "unknown segment $1 (map lair arena fight town shop wizard valley stonehenge coopmap coopbk coopfight cooploot cooptown coopswitch coopshop coopsheet coop4 coop4lair coopaiknight coopai coopaifight coopailoot coopaisheet coopaishop coopaishopsheet coop4ai coop4ailair)" >&2; return 1 ;;
  esac
}

[ $# -ge 3 ] || { sed -n 2,6p "$0" >&2; exit 2; }
seg=$1; out=$2; shift 2
keys=${MS_KEYS:-$(keys_of "$seg")}   # MS_KEYS: a hand-made script instead of the segment (scans)
intro=(--skip-intro)
# MS_PLAIN=natural: ... see below; MS_PLAIN=1: a real player's start: no --skip-intro, a fire at frame 40 skips the intro (the title is up ~110 frames later, before the first key of a segment)
if [ "$MS_PLAIN" = natural ]; then intro=(); keys="@5400 $keys"   # nobody presses a key: the intro runs to the title by itself (frame ~5250)
elif [ -n "$MS_PLAIN" ]; then intro=(); keys="@40 +Space @8 -Space $keys"; fi
mkdir -p "$out"
out=$(cd "$out" && pwd)
export SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy
for f in "$@"; do
  extra=()
  [ -n "$MS_AUDIO" ] && extra=(--dump-audio "$MS_AUDIO")
  [ "$seg" = coopbk ] && extra+=(--warp-knight 2 "${MS_WARP_AT:-2850}")   # the black-knight duel: knight 1 stands on the black knight (record 2) at that frame
  [ -n "$MS_ARGS" ] && extra+=($MS_ARGS)   # more options for the exe (word-split), e.g. MS_ARGS="--audio-render box"
  "$EXE" --disks "$MS_DISKS" --pref "$out/pref" "${intro[@]}" --fast --frames "$f" --no-grab --dump-frame "$out/s$f.png" --keys "$keys" "${extra[@]}" > "$out/out$f.txt" 2>&1 || echo "frame $f: exit $?"
done
rm -rf "$out/pref"
