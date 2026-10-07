# Town B (node 5, id $1A): the same town menu as town A but the fourth button is Mythral the mystic. The panel is on the left; the
# cursor starts at 30,100 each time the menu is drawn. The mystic and the healer share the donation screen: arrows (153,174) /
# (168,174) change the donation, Ok (140,189) pays, Exit (190,189) leaves.
# shotcmp-tol play-townB-mystic-ok 5
# shotcmp-tol play-townB-mystic-ok2 5
# shotcmp-tol play-townB-map 100
@PRE
@SET 1630
+0 poke gold 200
+10 poke warp_node 5
+70 joy1 fire pulse 8
wait input 10 gap 6 max 3000
+60 shot play-townB-menu

# Mystic (button 4): 25 frames down, click; donation 5, Ok, Exit
+10 @hold joy1 down 25
+30 shot play-townB-hover
+10 joy1 fire pulse 8
+300 shot play-townB-mystic
+10 @click 168 174
+30 @click 168 174
+30 @click 168 174
+30 @click 168 174
+30 @click 168 174
+30 shot play-townB-mystic-donation
+10 @click 140 189
+200 shot play-townB-mystic-ok
+100 shot play-townB-mystic-ok2
+10 @click 190 189
+250 shot play-townB-menu2

# Healer (button 3): 1 frame down, click; donation 1, Ok, Exit
+10 @hold joy1 down 1
+30 joy1 fire pulse 8
+300 shot play-townB-healer
+10 @click 168 174
+30 @click 140 189
+150 shot play-townB-healer-ok
+10 @click 190 189
+250 shot play-townB-menu3

# Exit (button 5): 44 frames down, click: back on the map
+10 @hold joy1 down 44
+30 joy1 fire pulse 8
+400 shot play-townB-map
+100 quit
