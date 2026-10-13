# Moonstone 2 co-op in town and on the loot screen (ROADMAP 8.5; play_coop_map is the map run this builds on).  `poke party 2`, Players = 2
# and two knights, as there.  The party (knight 1 on joystick 1, knight 2 on joystick 2) visits town A: the shop queue gives each knight its
# own turn on the town screen with its OWN pad - owner 2026-10-10: the leader starts, a click on the corner's ">" arrow makes the second
# knight active (its banner, token and name), and only joy0 moves its cursor then (a joy1 press must do nothing: shotdiff); one Exit ends
# the visit for the whole party and spends the turn once.
# The visit spends the party's turn, the round ends ("Next Day", fire) and knight 2 is the leader (joystick 2 = port 0): a lair fight (ratmen;
# the leader fights, ROADMAP 8.4b brings the second fighter) and the creature loot screen: the gold pile is clicked once and BOTH knights
# get the full pile (owner 2026-10-06: loot is not split); the dumps log every knight's gold, and `cur=` whose screens these are.
# Gold before: knight 1 = 50, knight 2 = 120.  Keep the poked stats small (the 100-record click pool, docs/AUTOPLAY.md).
# shotcmp-tol play-coop-town-map 3
# shotcmp-tol play-coop-town-k1-banner 5
# shotcmp-tol play-coop-town-k1 5
# shotcmp-tol play-coop-town-k2-banner 5
# shotcmp-tol play-coop-town-k2 5
# shotcmp-tol play-coop-town-k2-joy1 5
# shotcmp-tol play-coop-town-k2-joy0 5
# shotcmp-tol play-coop-town-back 5
# shotcmp-tol play-coop-town-arena 15
# shotcmp-tol play-coop-town-loot 5
# shotcmp-tol play-coop-town-loot-gold 5
# shotcmp-tol play-coop-town-end 5
# shotdiff play-coop-town-k2 play-coop-town-k2-joy0 0.05
wait file mindscape max 900
frame 20 key SPACE tap
wait input 10 max 3000
+20 joy1 fire pulse
+20 poke party 2
+40 joy1 right pulse
+40 shot play-coop-town-players
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
+530 shot play-coop-town-map
+10 poke who 0
+0 poke gold 50
+1 poke who 1
+1 poke gold 120
+1 poke who 0
+10 poke dump 1
# the town first: knight 1 (joystick 1) visits town A and leaves by Exit, then it is knight 2's turn (joystick 2)
+10 poke warp_node 4
+70 joy1 fire pulse 8
wait file HighWood.piv max 3000
wait input 10 gap 6 max 1500
# ROADMAP 8.5a: the banner "SIR <NAME>'S TURN" stands over the picture until the first input (or 1.5 s), then the name stays on the menu
+60 shot play-coop-town-k1-banner
+260 shot play-coop-town-k1
+10 poke dump 1
# owner 2026-10-10: the arrows in the corner switch the knight: a click on ">" (the right arrow) makes knight 2 the active one
+10 @click 121 12
+110 shot play-coop-town-k2-banner
+190 shot play-coop-town-k2
+10 poke dump 1
# a press on joystick 1 must not move knight 2's cursor
+10 @hold joy1 down 20
+30 shot play-coop-town-k2-joy1
+10 joy1 none
# its own pad does: 43 frames down = Exit
+10 @hold joy0 down 20
+30 shot play-coop-town-k2-joy0
+10 @hold joy0 down 23
+30 joy0 fire pulse 8
+300 shot play-coop-town-back
wait var turn 0 max 20000
+100 joy1 fire pulse 8
+300 poke dump 1
# the lair: knight 2 is the leader now (pad joystick 2): it fights, then the loot screen
+10 poke who 1
+1 poke warp_lair 0
+1 poke strength 9
+2 poke constitution 30
+3 poke endurance 9
+4 poke hpmax 310
+5 poke hp 310
+30 joy0 fire pulse 8
+800 shot play-coop-town-arena
+10 mash joy0 10
wait var scene 2 max 25000
+0 mash joy0 off
+1 joy0 none
+150 shot play-coop-town-loot
+10 poke dump 1
+10 poke cursorx 248
+0 poke cursory 76
+25 joy0 fire pulse 12
+100 shot play-coop-town-loot-gold
+10 poke dump 1
+10 poke cursorx 308
+0 poke cursory 140
+25 joy0 fire pulse 8
+700 shot play-coop-town-end
+10 poke dump 1
+100 quit
