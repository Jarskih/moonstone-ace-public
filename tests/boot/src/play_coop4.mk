# Co-op for four (ROADMAP 8.2 / 8.3 / 8.3a, owner 2026-10-09): main menu LOCAL, Players 4 and Mode = Coop (down, right), Select Knight four times with the four
# controllers in their default order: knight 1 Joystick 1 (joy1 fire), knight 2 Joystick 2 (offered; taken with joy1 fire, the knight
# screen listens to every controller), knight 3 Keys arrows (offered; taken with Ctrl), knight 4 Keys WASD (offered; every other controller is taken, so the cursor-right
# key goes to Computer (ROADMAP 8.3b, shot pad4-computer) and the cursor-left key back to WASD (pad4-refused = pad4); taken with Alt).  The map: ONE party token at knight 1's home.  Round k:
# the leader is knight k; the three other controllers held together (down, incl. both keyboard sets at once) must NOT move the token,
# the leader's own controller does (right); then E ends the turn, "Next Day", and the leader rotates to knight k+1.
# Round 4 (leader = knight 4, WASD): Space shows four sheets (4, 1, 2, 3), each closed with its owner's fire.  The dumps log the party
# line (size, leader, port, pad) and every knight's position; the log has "party: co-op, 4 knights" and "party: knight N ...".
# shotcmp-tol play-coop4-players3 0.5
# shotcmp-tol play-coop4-players4 0.5
# shotcmp-tol play-coop4-pad1 1.8
# shotcmp-tol play-coop4-pad2 1.8
# shotcmp-tol play-coop4-pad3 1.8
# shotcmp-tol play-coop4-pad4 1.8
# shotcmp-tol play-coop4-pad4-computer 1.8
# shotcmp-tol play-coop4-pad4-refused 1.8
# shotcmp-tol play-coop4-map 1.0
# shotcmp-tol play-coop4-r1-ignored 3
# shotcmp-tol play-coop4-r1-walk 3
# shotcmp-tol play-coop4-r2-lead 3
# shotcmp-tol play-coop4-r2-ignored 3
# shotcmp-tol play-coop4-r2-walk 3
# shotcmp-tol play-coop4-r3-lead 3
# shotcmp-tol play-coop4-r3-ignored 3
# shotcmp-tol play-coop4-r3-walk 3
# shotcmp-tol play-coop4-r4-lead 3
# shotcmp-tol play-coop4-r4-ignored 3
# shotcmp-tol play-coop4-r4-walk 3
# shotcmp-tol play-coop4-status1 3
# shotcmp-tol play-coop4-status2 3
# shotcmp-tol play-coop4-status3 3
# shotcmp-tol play-coop4-status4 3
# shotcmp-tol play-coop4-end 3
# shotdiff play-coop4-pad4 play-coop4-pad3 0.05
# shotdiff play-coop4-r1-ignored play-coop4-r1-walk 0.05
# shotdiff play-coop4-r2-ignored play-coop4-r2-walk 0.05
# shotdiff play-coop4-r3-ignored play-coop4-r3-walk 0.05
# shotdiff play-coop4-r4-ignored play-coop4-r4-walk 0.05
wait file mindscape max 900
frame 20 key SPACE tap
wait input 10 max 3000
+20 joy1 fire pulse
+60 shot play-coop4-menu
+100 joy1 right pulse
+30 joy1 right pulse
+40 shot play-coop4-players3
+10 joy1 right pulse
+40 shot play-coop4-players4
+10 joy1 down pulse
+30 joy1 right pulse
+30 joy1 down pulse
+30 joy1 down pulse
+40 joy1 fire pulse
+450 shot play-coop4-knight1
+10 joy1 fire pulse
+290 joy1 fire pulse
+60 shot play-coop4-pad1
+10 joy1 fire pulse
+390 joy1 fire pulse
+290 joy1 fire pulse
+100 shot play-coop4-pad2
+10 joy1 fire pulse
+390 joy1 fire pulse
+290 joy1 fire pulse
+100 shot play-coop4-pad3
+10 key CTRL down
+3 key CTRL up
+390 joy1 fire pulse
+290 joy1 fire pulse
+100 shot play-coop4-pad4
+10 key RIGHT down
+3 key RIGHT up
+60 shot play-coop4-pad4-computer
+10 key LEFT down
+3 key LEFT up
+60 shot play-coop4-pad4-refused
+10 key LALT down
+3 key LALT up
wait log knight_3_kind max 1500
+600 shot play-coop4-map
+10 poke dump 1
# round 1: leader = knight 1 (joystick 1)
+30 joy0 down
+0 key DOWN down
+0 key S down
+40 joy0 none
+0 key DOWN up
+0 key S up
+20 shot play-coop4-r1-ignored
+10 poke dump 1
+30 @hold joy1 right 40
+20 shot play-coop4-r1-walk
+10 poke dump 1
+10 key E tap
# a party of four leaves no black knight on the map: the round ends at once ("Next Day"), the leader rotates
wait var leader 1 max 3000
+100 poke dump 1
+10 joy1 fire pulse 8
+300 shot play-coop4-r2-lead
+10 poke dump 1
# round 2: leader = knight 2 (joystick 2)
+30 joy1 down
+0 key DOWN down
+0 key S down
+40 joy1 none
+0 key DOWN up
+0 key S up
+20 shot play-coop4-r2-ignored
+10 poke dump 1
+30 @hold joy0 right 40
+20 shot play-coop4-r2-walk
+10 poke dump 1
+10 key E tap
# a party of four leaves no black knight on the map: the round ends at once ("Next Day"), the leader rotates
wait var leader 2 max 3000
+100 poke dump 1
+10 joy1 fire pulse 8
+300 shot play-coop4-r3-lead
+10 poke dump 1
# round 3: leader = knight 3 (keys arrows), WASD + Alt + both joysticks held at once
+30 joy1 down
+0 joy0 down
+0 key S down
+0 key LALT down
+40 joy1 none
+0 joy0 none
+0 key S up
+0 key LALT up
+20 shot play-coop4-r3-ignored
+10 poke dump 1
+30 key RIGHT down
+40 key RIGHT up
+20 shot play-coop4-r3-walk
+10 poke dump 1
+10 key E tap
# a party of four leaves no black knight on the map: the round ends at once ("Next Day"), the leader rotates
wait var leader 3 max 3000
+100 poke dump 1
+10 joy1 fire pulse 8
+300 shot play-coop4-r4-lead
+10 poke dump 1
# round 4: leader = knight 4 (keys WASD), arrows + Ctrl + both joysticks held at once
+30 joy1 down
+0 joy0 down
+0 key DOWN down
+0 key CTRL down
+40 joy1 none
+0 joy0 none
+0 key DOWN up
+0 key CTRL up
+20 shot play-coop4-r4-ignored
+10 poke dump 1
+30 key D down
+40 key D up
+20 shot play-coop4-r4-walk
+10 poke dump 1
# four sheets: knight 4 (WASD + Alt), 1 (joy1), 2 (joy0), 3 (arrows + Ctrl)
+10 key SPACE tap
+300 shot play-coop4-status1
+10 poke cursorx 236
+1 poke cursory 150
+25 key LALT down
+8 key LALT up
+300 shot play-coop4-status2
+10 poke cursorx 236
+1 poke cursory 150
+25 joy1 fire pulse 8
+300 shot play-coop4-status3
+10 poke cursorx 236
+1 poke cursory 150
+25 joy0 fire pulse 8
+300 shot play-coop4-status4
+10 poke cursorx 236
+1 poke cursory 150
+25 key CTRL down
+8 key CTRL up
+300 shot play-coop4-end
+10 poke dump 1
+100 quit
