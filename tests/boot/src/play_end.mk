# The ending: Stonehenge with all four moonstones (the one of the current phase is enough): "You have completed the quest", the
# ceremony, the cinematic (the knight with the orb, the stone circle, the dancers, the vortex, the stars, the epilogue) and the
# title menu again. Only the page after "You have completed the quest" waits for fire; the rest runs on the game's timers.
# The animations run on the game's own timers: only the title and the first pages are compared tightly.
# shotcmp-tol play-end-quest 1.0
# shotcmp-tol play-end-ceremony 1.0
# shotcmp-tol play-end-orb 15
# shotcmp-tol play-end-circle 15
# shotcmp-tol play-end-dancers 20
# shotcmp-tol play-end-dancers2 20
# shotcmp-tol play-end-statue 15
# shotcmp-tol play-end-vortex 100
# shotcmp-tol play-end-stars 50
# shotcmp-tol play-end-hill 100
# shotcmp-tol play-end-epilogue 100
# shotcmp-tol play-end-fade 100
# shotcmp-tol play-end-title 3
@PRE
@SET 1630
+0 poke moonstones 15
+1 poke warp_node 7
+80 joy1 fire pulse 8
+500 shot play-end-quest
+10 joy1 fire pulse 8
+500 shot play-end-ceremony
+500 shot play-end-orb
+500 shot play-end-circle
+500 shot play-end-dancers
+500 shot play-end-dancers2
+500 shot play-end-statue
+500 shot play-end-vortex
+500 shot play-end-stars
+500 shot play-end-hill
+500 shot play-end-epilogue
+500 shot play-end-fade
+800 shot play-end-title
+100 quit
