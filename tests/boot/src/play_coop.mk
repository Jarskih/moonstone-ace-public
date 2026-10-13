# Co-op for two (ROADMAP 8.2): main menu LOCAL, Players 2 and Mode = Coop (down, right), Select Knight twice; after each name the controller line: knight 1
# keeps Joystick 1 (joy1 fire), knight 2 steps from Joystick 2 to Keys arrows with the cursor-right key and takes it with Ctrl
# (the keyboard set's fire; the knight screen listens to every controller). The map: knight 1 walks with joy1 and ends its turn
# (E); on knight 2's turn joy1 must NOT move it (dump), the cursor-right key must (dump). The log has the party lines
# ("party: co-op, 2 knights", "party: knight N kind K controller C <name>"). The party is ONE token at knight 1's home, the
# black knights another (ROADMAP 8.3): after knight 1's turn (E) the BK group moves, the round ends ("Next Day", fire), the leader is
# now knight 2: joy1 must not move it, the cursor-right key does; Space shows one status sheet per party member (two): the leader's (knight 2, closed with its own fire key Ctrl) and knight 1's (closed with joy1 fire).
# The dumps log the party line (size, leader, port, pad) and every knight's position.
# shotcmp-tol play-coop-menu 0.5
# shotcmp-tol play-coop-players 0.5
# shotcmp-tol play-coop-knight1 1.8
# shotcmp-tol play-coop-pad1 1.8
# shotcmp-tol play-coop-pad2 1.8
# shotcmp-tol play-coop-pad2-keys 1.8
# shotcmp-tol play-coop-map 1.0
# shotcmp-tol play-coop-walk1 1.0
# shotcmp-tol play-coop-bk 3
# shotcmp-tol play-coop-next-day 3
# shotcmp-tol play-coop-leader2 3
# shotcmp-tol play-coop-joy1-ignored 3
# shotcmp-tol play-coop-walk2 3
# shotcmp-tol play-coop-status1 3
# shotcmp-tol play-coop-status2 3
# shotcmp-tol play-coop-end 3
# shotdiff play-coop-pad2 play-coop-pad2-keys 0.05
# shotdiff play-coop-leader2 play-coop-joy1-ignored 0.05
wait file mindscape max 900
frame 20 key SPACE tap
wait input 10 max 3000
+20 joy1 fire pulse
+60 shot play-coop-menu
+100 joy1 right pulse
+10 joy1 down pulse
+30 joy1 right pulse
+40 shot play-coop-players
+10 joy1 down pulse
+30 joy1 down pulse
+40 joy1 fire pulse
+450 shot play-coop-knight1
+10 joy1 fire pulse
+290 joy1 fire pulse
+60 shot play-coop-pad1
+10 joy1 fire pulse
+390 joy1 fire pulse
+290 joy1 fire pulse
+100 shot play-coop-pad2
+10 key RIGHT down
+3 key RIGHT up
+60 shot play-coop-pad2-keys
+10 key CTRL down
+3 key CTRL up
wait log knight_1_kind max 1500
+600 shot play-coop-map
+10 poke dump 1
+30 @hold joy1 right 40
+20 shot play-coop-walk1
+10 poke dump 1
+10 key E tap
wait var turn 1 max 3000
+100 shot play-coop-bk
+10 poke dump 1
wait var turn 0 max 20000
+100 shot play-coop-next-day
+10 joy1 fire pulse 8
+300 shot play-coop-leader2
+10 poke dump 1
+30 @hold joy1 right 40
+20 shot play-coop-joy1-ignored
+10 poke dump 1
+30 key RIGHT down
+40 key RIGHT up
+20 shot play-coop-walk2
+10 poke dump 1
+10 key SPACE tap
+300 shot play-coop-status1
+10 poke cursorx 236
+1 poke cursory 150
+25 key CTRL down
+8 key CTRL up
+300 shot play-coop-status2
+10 @click 236 150
+300 shot play-coop-end
+10 poke dump 1
+100 quit
