# Stonehenge (node 7, id $1B) without the moonstone of the phase: Danu's offer. The item screen (scene 3) lets the player pick
# the item to give: click the sword; then the ritual (the druids, the knight, the lightning, the fade) and the status screen
# with the extra life. The ritual runs on the game's timers: loose tolerances.
# shotcmp-tol play-stone-offer 1.0
# shotcmp-tol play-stone-items 1.0
# shotcmp-tol play-stone-item-click 10
# shotcmp-tol play-stone-ritual1 5
# shotcmp-tol play-stone-ritual2 10
# shotcmp-tol play-stone-ritual3 10
# shotcmp-tol play-stone-ritual4 30
# (ritual4 lands on one of two fade frames, 23% apart, depending on CPU timing of the build)
# shotcmp-tol play-stone-ritual5 10
# shotcmp-tol play-stone-ritual6 5
@PRE
@SET 1630
+0 poke warp_node 7
+80 joy1 fire pulse 8
+300 shot play-stone-offer
+10 joy1 fire pulse 8
+300 shot play-stone-items
+10 @click 165 100
+200 shot play-stone-item-click
+200 shot play-stone-ritual1
+200 shot play-stone-ritual2
+300 shot play-stone-ritual3
+300 shot play-stone-ritual4
+400 shot play-stone-ritual5
+10 @click 236 150
+300 shot play-stone-ritual6
+100 quit
