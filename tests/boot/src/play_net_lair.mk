# Deterministic mode (ROADMAP 12.4, docs/NETPLAY.md 1.1c): the whole of play_lair with `poke netdet 1` switched on at the title menu
# (the intro and the ending of the program overlay always run classic): the title, Select Knight and name entry loops tick through
# ms::tickIdle, the map walks, the lair fight runs on the logical tick counter (the frame wait still waits real VBLs), the creature
# loot screen is steered by the cursor that netTick steps (the click is a poked cursor position + fire) and the map comes back.
# The same shots as play_lair: the picture must be the classic one.  `poke netstate` logs tick / logical clock / pacing counter / RNG.
# shotcmp-tol play-lair-arena 15
# shotcmp-tol play-lair-fight 15
# shotcmp-tol play-lair-loot 3
# shotcmp-tol play-lair-loot-gold 3
# shotcmp-tol play-lair-map 3
# --- boot to the map (the boot prefix of mkplay.py) with the deterministic mode on from the title menu ---
wait file mindscape max 900
frame 20 key SPACE tap
wait input 10 max 3000
frame 20 poke netdet 1
frame 100 joy1 down pulse
frame 130 joy1 down pulse
frame 160 joy1 down pulse
frame 230 joy1 fire pulse
frame 730 joy1 fire pulse
frame 1030 joy1 fire pulse
@SET 1630
+0 poke netstate 0
+10 @hold joy1 right 30
+10 poke netstate 0
+0 poke warp_lair 0
+1 poke strength 9
+2 poke constitution 30
+3 poke endurance 9
+4 poke hpmax 310
+5 poke hp 310
+30 shot play-lair-map-at-lair
+10 joy1 fire pulse 8
+800 shot play-lair-arena
+10 mash joy1 10
+700 shot play-lair-fight
+0 poke netstate 0
wait var scene 2 max 25000
+0 mash joy1 off
+1 joy1 none
+150 shot play-lair-loot
+0 poke netstate 0
+10 @click 248 76 12
+100 shot play-lair-loot-gold
+10 @click 308 140
+400 shot play-lair-map
+0 poke netstate 0
+100 quit
