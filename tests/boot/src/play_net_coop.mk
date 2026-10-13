# Deterministic mode with the co-op rules (ROADMAP 12.4 + 8.4b): play_coop_fight with `poke netdet 1` switched on at the title menu
# right after the intro. Same shots and tolerances as play_coop_fight: the picture must be the classic co-op one, with both pads
# read from the tick's latched frame (joy1 / joy0 are the autoplay override captured at the tick). `poke netstate` logs the counters.
# shotcmp-tol play-coop-fight-map 3
# shotcmp-tol play-coop-fight-arena 15
# shotcmp-tol play-coop-fight-moved 15
# shotcmp-tol play-coop-fight-mash 15
# shotcmp-tol play-coop-fight-loot 5
# shotcmp-tol play-coop-fight-end 5
wait file mindscape max 900
frame 20 key SPACE tap
wait input 10 max 3000
+30 joy1 fire pulse
frame 20 poke netdet 1
+120 poke party 2
+40 joy1 right pulse
+40 shot play-coop-fight-players
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
+0 poke netstate 0
+30 shot play-coop-fight-map
+10 poke dump 1
+10 joy1 fire pulse 8
+600 poke fightdump 1
+200 shot play-coop-fight-arena
+10 poke fightdump 1
+10 @hold joy1 up 25
+5 @hold joy0 down 25
+30 poke fightdump 1
+10 shot play-coop-fight-moved
+10 poke fightdump 1
+10 mash joy1 10
+0 mash joy0 13
+300 shot play-coop-fight-mash
+10 poke fightdump 1
wait var scene 2 max 12000
+0 mash joy1 off
+0 mash joy0 off
+1 joy1 none
+1 joy0 none
+0 poke netstate 0
+150 shot play-coop-fight-loot
+10 poke fightdump 1
+10 poke dump 1
+10 @click 308 140
+400 shot play-coop-fight-end
+10 poke dump 1
+0 poke netstate 0
+100 quit
