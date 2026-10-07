# Lair 19: the creature arena LAB_0175, covered by no other play script (play_lair = lair 0, the ratmen LAB_018C). Same flow as play_lair.mk.
# A lair fight (lair 0: the ratmen) with a strong knight; the autopilot `mash` plays the fight (random enemy moves: loose
# tolerances), `wait var scene 2` waits for the creature loot screen, the gold is taken (a click on the pile) and the screen left.
# Keep the poked stats small: the stat sheet registers one click region per stat point and the screen loop has a pool of 100 regions
# (24 bytes each, 0x960): with strength 40 / constitution 60 / endurance 40 the loot icons were never registered and clicking them did nothing.
# shotcmp-tol play-lair19-arena 15
# shotcmp-tol play-lair19-fight 15
# shotcmp-tol play-lair19-loot 3
# shotcmp-tol play-lair19-loot-gold 3
# shotcmp-tol play-lair19-map 3
@PRE
@SET 1630
+0 poke warp_lair 19
+1 poke strength 9
+2 poke constitution 30
+3 poke endurance 9
+4 poke hpmax 310
+5 poke hp 310
+30 shot play-lair19-map-at-lair
+10 joy1 fire pulse 8
+800 shot play-lair19-arena
+10 mash joy1 10
+700 shot play-lair19-fight
wait var scene 2 max 25000
+0 mash joy1 off
+1 joy1 none
+150 shot play-lair19-loot
+10 @click 248 76 12
+100 shot play-lair19-loot-gold
+10 @click 308 140
+400 shot play-lair19-map
+100 quit
