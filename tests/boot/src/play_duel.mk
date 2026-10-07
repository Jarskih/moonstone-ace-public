# A duel with a black knight (an AI knight): the knight is put on top of knight 1 (the proximity scan then offers the duel), the
# fight is played by the autopilot, `wait var scene 1` waits for the meeting screen (both knights, the loot exchange).
# The fight is random: loose tolerances.
# shotcmp-tol play-duel-arena 15
# shotcmp-tol play-duel-fight 100
# shotcmp-tol play-duel-meet 3
# shotcmp-tol play-duel-meet2 3
# shotcmp-tol play-duel-prepare 3
@PRE
@SET 1630
+0 poke warp_knight 1
+1 poke strength 9
+2 poke constitution 30
+3 poke endurance 9
+4 poke hpmax 310
+5 poke hp 310
+30 shot play-duel-map-at-knight
+10 joy1 fire pulse 8
+500 shot play-duel-prepare
+400 shot play-duel-arena
+10 mash joy1 10
+500 shot play-duel-fight
wait var scene 1 max 25000
+0 mash joy1 off
+1 joy1 none
+150 shot play-duel-meet
+10 @click 155 110
+300 shot play-duel-meet2
+100 quit
