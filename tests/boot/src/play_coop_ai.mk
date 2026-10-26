# Moonstone 2 co-op with a COMPUTER partner (ROADMAP 8.3b, docs/MOONSTONE2.md 9; twin of play_coop_fight): `poke party 2`, Players 2, knight 1 takes
# Joystick 1; on knight 2's controller line the stick-left key steps back over the taken Joystick 1 to "Computer" (the ring: Joystick 1..Keys WASD,
# Computer) and fire takes it (shot play-coop-ai-pad2).  The party walks onto lair 0 (the ratmen): both knights are in the arena, only joy1 is
# played (mash joy1; nothing drives knight 2), `poke fightdump 1` logs the roster: knight 2 must move and strike on its own.  The lair is cleared
# (`wait var scene 2` = the creature loot screen, the human's pad closes it) and the map is back.
# the fight is random (the enemies): loose tolerances
# shotcmp-tol play-coop-ai-pad2 1.8
# shotcmp-tol play-coop-ai-map 3
# shotcmp-tol play-coop-ai-arena 15
# shotcmp-tol play-coop-ai-mash 15
# shotcmp-tol play-coop-ai-loot 5
# shotcmp-tol play-coop-ai-end 5
wait file mindscape max 900
frame 20 key SPACE tap
wait input 10 max 3000
+20 joy1 fire pulse
+120 poke party 2
+40 joy1 right pulse
+40 shot play-coop-ai-players
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
+70 joy1 left pulse
+50 shot play-coop-ai-pad2
+10 joy1 fire pulse
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
+11 poke hp 310
+12 poke who 0
+13 poke warp_lair 0
+14 poke lair_left 5
+30 shot play-coop-ai-map
+10 poke dump 1
+10 joy1 fire pulse 8
+600 poke fightdump 1
+200 shot play-coop-ai-arena
+10 poke fightdump 1
+30 poke fightdump 1
+10 poke fightdump 1
+10 mash joy1 10
+300 shot play-coop-ai-mash
+10 poke fightdump 1
wait var scene 2 max 12000
+0 mash joy1 off
+1 joy1 none
+150 shot play-coop-ai-loot
+10 poke fightdump 1
+10 poke dump 1
+10 @click 308 140
+400 shot play-coop-ai-end
+10 poke dump 1
+100 quit

