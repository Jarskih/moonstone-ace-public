# Two players: Players = 2 (the menu's first item, right), Select Knight twice (knight 1 and knight 3, their names), the map; the
# first knight walks (port 1) and ends its turn (E), then the second human's turn: both sticks push right, the dump says which one
# moved the knight (the second knight reads port 0).
# shotcmp-tol play-2p-menu 0.5
# shotcmp-tol play-2p-map 1.0
# shotcmp-tol play-2p-turn2 1.0
# shotcmp-tol play-2p-walk2 1.0
frame 300 key SPACE tap
wait input 10 max 3000
+60 shot play-2p-menu
+100 joy1 right pulse
+40 shot play-2p-players
+10 joy1 down pulse
+30 joy1 down pulse
+30 joy1 down pulse
+40 shot play-2p-select
+10 joy1 fire pulse
+450 shot play-2p-knight1
+10 joy1 fire pulse
+290 shot play-2p-name1
+10 joy1 fire pulse
+390 shot play-2p-knight2
+10 joy1 right pulse
+40 shot play-2p-knight2-cursor
+10 joy1 fire pulse
+340 shot play-2p-name2
+10 joy1 fire pulse
+600 shot play-2p-map
+10 poke dump 1
+30 @hold joy1 right 40
+30 key E tap
wait var turn 1 max 3000
+100 shot play-2p-turn2
+10 poke who 1
+11 poke dump 1
+30 joy0 right
+40 joy1 right
+40 joy0 none
+1 joy1 none
+40 shot play-2p-walk2
+10 poke dump 1
+100 quit
