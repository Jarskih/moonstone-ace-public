# Co-op for two (ROADMAP 8.2): Players right x4 = "Coop 2", Select Knight twice; after each name the controller line: knight 1
# keeps Joystick 1 (joy1 fire), knight 2 steps from Joystick 2 to Keys arrows with the cursor-right key and takes it with Ctrl
# (the keyboard set's fire; the knight screen listens to every controller). The map: knight 1 walks with joy1 and ends its turn
# (E); on knight 2's turn joy1 must NOT move it (dump), the cursor-right key must (dump). The log has the party lines
# ("party: co-op, 2 knights", "party: knight N kind K controller C <name>").
# shotcmp-tol play-coop-menu 0.5
# shotcmp-tol play-coop-players 0.5
# shotcmp-tol play-coop-knight1 1.8
# shotcmp-tol play-coop-pad1 1.8
# shotcmp-tol play-coop-pad2 1.8
# shotcmp-tol play-coop-pad2-keys 1.8
# shotcmp-tol play-coop-map 1.0
# shotcmp-tol play-coop-turn2 1.0
# shotcmp-tol play-coop-walk2 1.0
# shotdiff play-coop-pad2 play-coop-pad2-keys 0.05
# shotdiff play-coop-turn2 play-coop-walk2 0.05
frame 300 key SPACE tap
wait input 10 max 3000
+60 shot play-coop-menu
+100 joy1 right pulse
+30 joy1 right pulse
+30 joy1 right pulse
+30 joy1 right pulse
+40 shot play-coop-players
+10 joy1 down pulse
+30 joy1 down pulse
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
+30 key E tap
wait var turn 1 max 3000
+100 shot play-coop-turn2
+10 poke who 1
+11 poke dump 1
+30 @hold joy1 right 40
+20 poke dump 1
+30 key RIGHT down
+40 key RIGHT up
+40 shot play-coop-walk2
+10 poke dump 1
+100 quit
