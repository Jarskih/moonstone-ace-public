# Town A (node 4, id $19): Merchant (the smith), Tavern (dice), Healer, High Temple, Space = temple, Exit.
# Cursor screens: the cursor moves 2 px per frame while a direction is held, a click needs fire held ~8 frames, and the exit of a
# shop screen is the right pillar (cursor 308,140). Positions are lowres pixels (the window shows them doubled). Town A's cursor
# starts at 290,100 every time the town menu is drawn.
# shotcmp-tol play-townA-dice-bet 5
# shotcmp-tol play-townA-dice-roll 5
# shotcmp-tol play-townA-dice-result 5
# shotcmp-tol play-townA-market 3
@PRE
@SET 1630
+0 poke gold 200
+10 poke warp_node 4
+70 joy1 fire pulse 8
wait input 10 gap 6 max 3000
+60 shot play-townA-menu

# Merchant (button 1): up 32 frames, click: the smith. Buy the broad sword (10 gp) and chainmail (30 gp), leave by the pillar.
+10 @hold joy1 up 32
+30 shot play-townA-hover
+10 joy1 fire pulse 8
+250 shot play-townA-smith
+10 @click 250 111
+120 shot play-townA-smith-sword
+10 @click 205 150
+120 shot play-townA-smith-mail
+10 @click 308 140
+250 shot play-townA-menu2

# Tavern (button 2): 12 frames up, click: dice. Bet 1 gold, continue after the result, leave by Exit.
+10 @hold joy1 up 13
+30 joy1 fire pulse 8
+250 shot play-townA-dice
+10 @click 292 56
+250 shot play-townA-dice-bet
+200 shot play-townA-dice-roll
+200 shot play-townA-dice-result
+10 joy1 fire pulse 8
+200 shot play-townA-dice-bets
+10 @click 287 189
+250 shot play-townA-menu3

# Healer (button 3): 5 frames down, click: the donation screen. Raise the donation 3 times, Ok, Exit.
+10 @hold joy1 down 5
+30 joy1 fire pulse 8
+250 shot play-townA-healer
+10 @click 168 174
+30 @click 168 174
+30 @click 168 174
+30 shot play-townA-healer-donation
+10 @click 140 189
+100 shot play-townA-healer-ok
+10 @click 190 189
+250 shot play-townA-menu4

# High Temple (button 4, the market in town A): 26 frames down, click; leave by the pillar
+10 @hold joy1 down 26
+30 joy1 fire pulse 8
+300 shot play-townA-market
+10 @click 308 140
+250 shot play-townA-menu5

# Space: the temple screen (status / stat purchase), leave by the pillar
+10 key SPACE tap
+350 shot play-townA-temple
+10 @click 236 150
+250 shot play-townA-menu6

# Exit (button 5): 43 frames down, click: back on the map
+10 @hold joy1 down 43
+30 joy1 fire pulse 8
+400 shot play-townA-map
+100 quit
