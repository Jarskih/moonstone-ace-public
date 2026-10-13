# Moonstone 2 co-op against the black-knight group (ROADMAP 8.4b slice 3): two party knights (joy1, joy0) meet TWO black knights (N = party size,
# the first is the original AI knight of record 2 at its normal start, the second a fight extra; `poke warp_knight 2` puts the party on it).  `poke fightdump 1` logs the roster (party=2 foes=2), both
# fighters per side (the black knights have the AI handler and their own target slot), the limits (16 jobs / 64 rects); each knight
# is moved with its own pad; both pads play the fight with the autopilot until the meeting screen (`wait var scene 1`).
# the fight is random: loose tolerances
# shotcmp-tol play-coop-bk-fight-map 3
# shotcmp-tol play-coop-bk-fight-arena 15
# shotcmp-tol play-coop-bk-fight-moved 15
# shotcmp-tol play-coop-bk-fight-mash 100
# shotcmp-tol play-coop-bk-fight-meet 5
# shotcmp-tol play-coop-bk-fight-end 5
wait file mindscape max 900
frame 20 key SPACE tap
wait input 10 max 3000
+20 joy1 fire pulse
+120 poke party 2
+40 joy1 right pulse
+40 shot play-coop-bk-fight-players
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
+13 poke warp_knight 2
+30 shot play-coop-bk-fight-map
+10 poke dump 1
+10 joy1 fire pulse 8
+200 key 1 tap
+800 poke fightdump 1
+200 shot play-coop-bk-fight-arena
+10 poke fightdump 1
+10 @hold joy1 up 25
+5 @hold joy0 down 25
+30 poke fightdump 1
+10 shot play-coop-bk-fight-moved
+10 mash joy1 10
+0 mash joy0 13
+300 shot play-coop-bk-fight-mash
+10 poke fightdump 1
wait var scene 1 max 12000
+0 mash joy1 off
+0 mash joy0 off
+1 joy1 none
+1 joy0 none
+150 shot play-coop-bk-fight-meet
+10 poke fightdump 1
+10 poke dump 1
+10 @click 155 110
+300 shot play-coop-bk-fight-end
+10 poke dump 1
+100 quit
