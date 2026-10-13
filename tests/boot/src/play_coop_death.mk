# Moonstone 2 co-op: one party knight dies in a lair fight, the other must fight on (owner report 2026-10-11: "when one knight was killed, the
# survivor disappeared, the dead one took the other's colour and the fight ended").  `poke party 2`, the party walks onto lair 0 (ratmen) with
# knight 2 at 12 hit points and nobody steering it: the ratmen hurt, grab and kill it (a squeeze by a ratman, the kill that ended the fight
# before the fix, comes up now and then; the plain hurt death is the usual one).  Knight 1 (310 hp) fights with the autopilot (`mash joy1`).
# `poke fightdump 1` logs the roster, both fighters, the job table (slots, owners, scripts) and the creatures every ~70 frames; the `fightop` log
# lines (autoplay builds: the script ops LAB_0005 / 0006 / 000D with the owner and both knights' hit points) show what ends the fight.
# Pass: after knight 2 is down (hp < 0) the dump says roster on=1 and the fight runs on with knight 1 until the lair is cleared (`wait var scene 2`
# = creature loot screen); knight 2's corpse lies in its own colours and no standing knight 2 is drawn beside it (shots `play-coop-death-*`);
# the end dump shows knight 2 one life down (5 -> 4), knight 1 untouched.
# shotcmp-tol play-coop-death-arena 15
# shotcmp-tol play-coop-death-dying 15
# shotcmp-tol play-coop-death-after 15
# shotcmp-tol play-coop-death-late 15
# shotcmp-tol play-coop-death-corpse 15
# shotcmp-tol play-coop-death-corpse2 15
# shotcmp-tol play-coop-death-loot 15
frame 300 key SPACE tap
wait input 10 max 3000
+20 joy1 fire pulse
+120 poke party 2
+40 joy1 right pulse 10
+40 shot play-coop-death-players
+30 poke menuact 3
+450 joy1 fire pulse 30
+300 joy1 fire pulse 30
+70 joy1 fire pulse 30
+330 joy1 right pulse 10
+50 joy1 fire pulse 30
+350 joy1 fire pulse 30
+70 joy1 fire pulse 30
+530 poke who 0
+1 poke strength 9
+2 poke constitution 30
+3 poke endurance 9
+4 poke hpmax 310
+5 poke hp 310
+6 poke who 1
+7 poke strength 9
+8 poke constitution 30
+9 poke endurance 9
+10 poke hpmax 310
+11 poke hp 12
+12 poke who 0
+13 poke warp_lair 0
+14 poke lair_left 6
+30 poke dump 1
+10 joy1 fire pulse 8
+500 poke fightdump 1
+100 shot play-coop-death-arena
+0 mash joy1 10
+70 poke fightdump 1
+70 poke fightdump 1
+70 shot play-coop-death-dying
+0 poke fightdump 1
+70 poke fightdump 1
+70 poke fightdump 1
+70 poke fightdump 1
+70 shot play-coop-death-after
+0 poke fightdump 1
+70 poke fightdump 1
+70 poke fightdump 1
+70 poke fightdump 1
+70 poke fightdump 1
+70 shot play-coop-death-late
+0 poke fightdump 1
wait log fightop_6 max 6000
+100 shot play-coop-death-corpse
+0 poke fightdump 1
+200 shot play-coop-death-corpse2
+0 poke fightdump 1
+120 poke fightdump 1
wait var scene 2 max 9000
+0 mash joy1 off
+1 joy1 none
+150 shot play-coop-death-loot
+10 poke fightdump 1
+10 poke dump 1
+100 quit
