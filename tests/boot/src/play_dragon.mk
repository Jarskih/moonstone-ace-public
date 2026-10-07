# The dragon: it flies on the map from day 2 on (spawned when the map starts). The day counter is poked to 2, the black knights are
# parked (frog 100), the turn is ended (E) so the next map start spawns the dragon (`wait var dragon 1`), then `poke dragon_hit 1`
# puts it on the knight and makes it the target: the next map frame starts the dragon fight (LAB_0DC8 + LAB_0083, bats and all).
# LOSS path: the knight stands and walks left (no attack), the dragon breathes fire; `wait var lives 4` = the life is lost, then the
# Next Day screen and the map. play_dragon_win.mk is the win path. Stats stay small (docs/AUTOPLAY.md). The fight is random: loose tolerances.
# shotcmp-tol play-dragon-map 3
# shotcmp-tol play-dragon-flying 15
# shotcmp-tol play-dragon-arena 15
# shotcmp-tol play-dragon-status 3
# shotcmp-tol play-dragon-after-status 3
# shotcmp-tol play-dragon-attack 100
# shotcmp-tol play-dragon-close 100
# shotcmp-tol play-dragon-lost 100
# shotcmp-tol play-dragon-map-after 5
@PRE
@SET 1630
+0 poke who 1
+1 poke frog 100
+2 poke who 2
+3 poke frog 100
+4 poke who 3
+5 poke frog 100
+6 poke who 0
+7 poke strength 9
+8 poke constitution 30
+9 poke endurance 9
+10 poke hpmax 310
+11 poke hp 310
+12 poke day 2
+30 shot play-dragon-map
# the status screen and back restarts the map loop, which spawns the dragon (day >= 2)
+10 key SPACE tap
+300 shot play-dragon-status
+10 @click 236 150
+300 shot play-dragon-after-status
wait var dragon 1 max 1500
+200 shot play-dragon-flying
+10 poke dump 1
+10 poke dragon_hit 1
+300 shot play-dragon-attack
+500 shot play-dragon-arena
# the dragon is on top of the knight; the knight walks left, the dragon attacks (fire breath)
+10 @hold joy1 left 350
+20 shot play-dragon-close
+10 poke fightdump 1
wait var lives 4 max 9000
+0 joy1 none
+150 shot play-dragon-lost
+10 poke dump 1
+10 joy1 fire pulse 8
+400 shot play-dragon-map-after
+10 poke dump 1
+100 quit
