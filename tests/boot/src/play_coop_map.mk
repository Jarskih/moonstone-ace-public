# Moonstone 2 co-op on the map, poke-driven shortcut (ROADMAP 8.3; play_coop is the menu-driven run): `poke party 2` switches the co-op rules on (default pads: knight 1 joystick 1, knight 2 joystick 2), then Players = 2 and two knights as in
# play_2p.  The party starts as ONE token at the first player's home; the black-knight group is one token too.  The leader (knight 1)
# walks with port 1 and ends the turn (E); the BK group moves; the round ends ("Next Day", fire); the leader is now knight 2: port 1
# no longer moves the token, port 0 does.  Space shows the status sheet of every party member (two sheets).  The dumps log the party
# (size, leader, the leader's port, the BK records 5..6) and every knight's position (the members share the token's).
# the map's colour-cycled movement/region outlines vary 0.1-0.6 % with timing
# shotcmp-tol play-coop-map-map 1.0
# shotcmp-tol play-coop-map-walk1 1.0
# shotcmp-tol play-coop-map-bk 3
# shotcmp-tol play-coop-map-next-day 3
# shotcmp-tol play-coop-map-leader2 3
# shotcmp-tol play-coop-map-port1 3
# shotcmp-tol play-coop-map-walk2 3
# shotcmp-tol play-coop-map-status1 3
# shotcmp-tol play-coop-map-status2 3
# shotcmp-tol play-coop-map-end 3
wait file mindscape max 900
frame 20 key SPACE tap
wait input 10 max 3000
+20 joy1 fire pulse
+20 poke party 2
+40 joy1 right pulse
+40 shot play-coop-map-players
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
+530 shot play-coop-map-map
+10 poke dump 1
+30 @hold joy1 right 40
+20 shot play-coop-map-walk1
+10 poke dump 1
+10 key E tap
wait var turn 1 max 3000
+100 shot play-coop-map-bk
+10 poke dump 1
wait var turn 0 max 20000
+100 shot play-coop-map-next-day
+10 joy1 fire pulse 8
+300 shot play-coop-map-leader2
+10 poke dump 1
+30 @hold joy1 right 40
+20 shot play-coop-map-port1
+10 poke dump 1
+30 @hold joy0 down 40
+20 shot play-coop-map-walk2
+10 poke dump 1
+10 key SPACE tap
+300 shot play-coop-map-status1
+10 poke cursorx 236
+0 poke cursory 150
+25 joy0 fire pulse 8
+300 shot play-coop-map-status2
+10 @click 236 150
+300 shot play-coop-map-end
+10 poke dump 1
+100 quit
