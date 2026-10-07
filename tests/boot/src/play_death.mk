# Defeat and game over: the knight has one life left and stands idle in a lair fight (the ratmen kill it): the "you lost" status
# screen (scene 9) with the life gone, then the map; the black knights are parked (frog days) so the turn comes back to the dead
# knight at once: no human knight alive = game over (farewell text, fire, back to the title menu).
# shotcmp-tol play-death-arena 15
# shotcmp-tol play-death-fight 15
# shotcmp-tol play-death-lost 3
# shotcmp-tol play-death-map 3
# shotcmp-tol play-death-gameover1 3
# shotcmp-tol play-death-gameover2 3
# shotcmp-tol play-death-gameover3 3
# shotcmp-tol play-death-title 3
@PRE
@SET 1630
+0 poke lives 1
+1 poke who 1
+2 poke frog 100
+3 poke who 2
+4 poke frog 100
+5 poke who 3
+6 poke frog 100
+7 poke who 0
+8 poke warp_lair 0
+30 joy1 fire pulse 8
+800 shot play-death-arena
+400 shot play-death-fight
wait var scene 9 max 20000
+200 shot play-death-lost
+10 @click 236 150
+300 shot play-death-map
+300 shot play-death-gameover1
+300 shot play-death-gameover2
+10 joy1 fire pulse 8
+300 shot play-death-gameover3
+10 joy1 fire pulse 8
+600 shot play-death-title
+100 quit
