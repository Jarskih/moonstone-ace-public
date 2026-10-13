# Moonstone 2 co-op fight with FOUR knights (ROADMAP 8.4b, the 2-knight twin is play_coop_fight): `poke party 4`, Players 4, four knights with joystick 1, joystick 2, the arrow keys (Ctrl fire) and WASD (Alt fire); the party fights lair 0 (the ratmen): 16 jobs / 64 dirty rects, waves x4, `+3` alive: `poke party 2` switches the co-op rules on (knight 1 =
# joystick 1 = joy1, knight 2 = joystick 2 = joy0), the party walks onto lair 0 (the ratmen) and fires into the fight.  BOTH knights
# are in the arena (shot play-coop4-fight-arena; `poke fightdump 1` logs the roster, both fighters with their ports and every live
# creature with its sticky target slot and team).  Each knight is moved with its OWN pad: joy1 walks the leader, joy0 the second
# knight (fightdump before / after).  Then both pads play the fight with the autopilot (`mash`) until the lair is cleared
# (`wait var scene 2` = creature loot screen); the end dump shows the roster's defeat bits, and the log has the wave numbers
# ("waves: ... coop=1 total=... alive=..."): the total is doubled and one more is alive at once.
# Keep the poked stats small (docs/AUTOPLAY.md: the stat sheet registers one click region per point).
# the fight is random (the enemies): loose tolerances
# shotcmp-tol play-coop4-fight-map 3
# shotcmp-tol play-coop4-fight-arena 15
# shotcmp-tol play-coop4-fight-moved 15
# shotcmp-tol play-coop4-fight-mash 15
# shotcmp-tol play-coop4-fight-loot 5
# shotcmp-tol play-coop4-fight-end 5
wait file mindscape max 900
frame 20 key SPACE tap
wait input 10 max 3000
+20 joy1 fire pulse
+120 poke party 4
+40 joy1 right pulse
+30 joy1 right pulse
+30 joy1 right pulse
+40 shot play-coop4-fight-players
+10 joy1 down pulse
+30 joy1 down pulse
+30 joy1 down pulse
+50 joy1 fire pulse
+450 joy1 fire pulse
+300 joy1 fire pulse
+70 joy1 fire pulse
+330 joy1 right pulse
+50 joy1 fire pulse
+350 joy1 fire pulse
+70 joy1 fire pulse
+330 joy1 fire pulse
+350 joy1 fire pulse
+70 joy1 fire pulse
+330 joy1 fire pulse
+350 joy1 fire pulse
+70 joy1 fire pulse
+530 poke who 0
+2 poke strength 9
+2 poke constitution 30
+2 poke endurance 9
+2 poke hpmax 310
+2 poke hp 310
+6 poke who 1
+2 poke strength 9
+2 poke constitution 30
+2 poke endurance 9
+2 poke hpmax 310
+2 poke hp 310
+6 poke who 2
+2 poke strength 9
+2 poke constitution 30
+2 poke endurance 9
+2 poke hpmax 310
+2 poke hp 310
+6 poke who 3
+2 poke strength 9
+2 poke constitution 30
+2 poke endurance 9
+2 poke hpmax 310
+2 poke hp 310
+6 poke who 0
+13 poke warp_lair 0
+14 poke lair_left 5
+30 shot play-coop4-fight-map
+10 poke dump 1
+10 joy1 fire pulse 8
+600 poke fightdump 1
+200 shot play-coop4-fight-arena
+10 poke fightdump 1
+10 @hold joy1 up 25
+5 @hold joy0 down 25
+5 key LEFT down
+25 key LEFT up
+5 key W down
+25 key W up
+30 poke fightdump 1
+10 shot play-coop4-fight-moved
+10 poke fightdump 1
+10 mash joy1 10
+0 mash joy0 13
+300 shot play-coop4-fight-mash
+10 poke fightdump 1
wait var scene 2 max 12000
+0 mash joy1 off
+0 mash joy0 off
+1 joy1 none
+1 joy0 none
+150 shot play-coop4-fight-loot
+10 poke fightdump 1
+10 poke dump 1
+10 @click 308 140
+400 shot play-coop4-fight-end
+10 poke dump 1
+100 quit
