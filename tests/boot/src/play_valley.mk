# The Valley of the Gods (node 6, id $1C). First without the four keys: the text only. Then with all keys and a strong knight:
# the guardian fight (the autopilot `mash` plays it), the victory (the keys are taken: `wait var keys 0`), the win text.
# The fight itself is random (the guardian's moves), so its shots have loose tolerances.
# shotcmp-tol play-valley-fight1 15
# shotcmp-tol play-valley-fight2 25
# shotcmp-tol play-valley-won 100
# shotcmp-tol play-valley-won2 100
# shotcmp-tol play-valley-gods 100
@PRE
@SET 1630
+0 poke warp_node 6
+80 joy1 fire pulse 8
+250 shot play-valley-nokeys
+10 joy1 fire pulse 8
+300 shot play-valley-nokeys-map

# all four keys, a strong knight, enter again (the turn is still the knight's)
+10 poke keys 15
+11 poke strength 9
+12 poke constitution 30
+13 poke endurance 9
+14 poke hpmax 310
+15 poke hp 310
+30 joy1 fire pulse 8
+700 shot play-valley-fight1
+10 mash joy1 10
+500 shot play-valley-fight2
wait var keys 0 max 20000
+0 mash joy1 off
+1 joy1 none
+200 shot play-valley-won
+10 joy1 fire pulse 8
+300 shot play-valley-won2
+400 shot play-valley-gods
+100 quit
