# Moonstone 2 co-op: the VALLEY GUARDIAN fought by two knights (ROADMAP 8.4, emulator pass pending).  Same start as play_coop_fight
# (`poke party 2`, Players 2, two knights, knight 1 = joy1, knight 2 = joy0).  Both knights get all four keys, the party stands on the
# valley (node 6) and fires into the fight.  Expected: BOTH knights in the arena (fightdump: party 2, ports, hp, lives; the guardian's
# sticky target slot), each moved by its own pad, then the autopilot plays the fight.  Knight 2 is poked WEAK (hp 12) so it usually falls
# first: in a WON fight the dump after it shows knight 2 with lives 4 (only the normal life of a fallen knight; the guardian's penalty, 2 lives + a stat
# point per fallen knight, is charged only when the whole party LOSES) while knight 1 keeps its lives (keys 0 for both, progress +3 = the 8.5 victory).  A lost party
# ends on the temple screen (the wait below then times out after 14000 frames: expected, not a failure).  Log lines to look for: "knight colours: ..." (or "share a colour" when the valley
# picture has no three free entries: expected on some pictures, MOONSTONE2 6.3), the low-hp pulse of knight 2 in the shots.
# shotcmp-tol play-cvalley-arena 20
# shotcmp-tol play-cvalley-moved 20
# shotcmp-tol play-cvalley-mash 100
# shotcmp-tol play-cvalley-end 100
wait file mindscape max 900
frame 20 key SPACE tap
wait input 10 max 3000
+20 joy1 fire pulse
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
+12 poke who 1
+13 poke hpmax 12
+14 poke hp 12
+15 poke who 0
+16 poke keys 15
+17 poke who 1
+18 poke keys 15
+19 poke who 0
+20 poke dump 1
+20 poke warp_node 6
+80 joy1 fire pulse 8
+700 poke fightdump 1
+10 shot play-cvalley-arena
+10 @hold joy1 up 25
+5 @hold joy0 down 25
+30 poke fightdump 1
+10 shot play-cvalley-moved
+10 mash joy1 10
+0 mash joy0 13
+300 shot play-cvalley-mash
+10 poke fightdump 1
wait var keys 0 max 14000
+0 mash joy1 off
+0 mash joy0 off
+1 joy1 none
+1 joy0 none
+150 shot play-cvalley-end
+10 poke dump 1
+10 poke fightdump 1
+10 joy1 fire pulse 8
+400 poke dump 1
+100 quit
