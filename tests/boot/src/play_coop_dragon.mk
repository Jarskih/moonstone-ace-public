# Moonstone 2 co-op: the DRAGON fought by two knights (ROADMAP 8.4, emulator pass pending).  Start as play_coop_fight; the black-knight
# group is parked (frog on the AI knight records 2, 3), the day is poked to 2 so the dragon spawns on the next map start, `poke dragon_hit 1` puts it
# on the party and the next map frame starts the fight.  Expected: BOTH knights in the arena with the dragon and the two bats
# (fightdump: roster 2, the dragon's and bats' sticky target slots may differ), the dragon follows the nearer knight (walk the two
# apart: joy1 up, joy0 down).  Both knights are poked strong; knight 2 gets hp 30 so it can fall: a fallen knight loses a life (dump
# lives 4) and the party still wins; the loot screen follows (`wait var dragonlives` negative = slain, scene 10).  The dump after the fight
# gives each knight's lives / hp.  Look for "knight colours: knight 2 uses palette entries" in the log (the dragon picture may leave
# only two free entries: then "share a colour", see MOONSTONE2 6.3) and the low-hp pulse of knight 2 (its own colour).
# shotcmp-tol play-cdragon-arena 20
# shotcmp-tol play-cdragon-apart 30
# shotcmp-tol play-cdragon-mash 100
# shotcmp-tol play-cdragon-end 100
wait file mindscape max 900
frame 20 key SPACE tap
wait input 10 max 3000
+20 joy1 fire pulse
+120 poke party 2
+40 joy1 right pulse
+40 shot play-coop-fight-players
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
+12 poke who 1
+13 poke hpmax 30
+14 poke hp 30
+15 poke who 2
+16 poke frog 100
+17 poke who 3
+18 poke frog 100
+19 poke who 0
+20 poke day 2
+30 key SPACE tap
+300 shot play-cdragon-status
+10 @click 236 150
# the second party member's status sheet (its own pad: joystick 2 = joy0)
+285 poke cursorx 236
+0 poke cursory 150
+30 joy0 fire pulse 8
+300 key SPACE tap
+10 key SPACE tap
wait var dragon 1 max 1500
+200 poke dump 1
+10 poke dragon_hit 1
+800 poke fightdump 1
+10 shot play-cdragon-arena
+10 @hold joy1 up 40
+5 @hold joy0 down 40
+30 poke fightdump 1
+10 shot play-cdragon-apart
+10 mash joy1 10
+0 mash joy0 13
+300 shot play-cdragon-mash
+10 poke fightdump 1
wait var dragonlives 255 max 14000
+0 mash joy1 off
+0 mash joy0 off
+1 joy1 none
+1 joy0 none
+200 shot play-cdragon-end
+10 poke dump 1
+10 poke fightdump 1
+100 quit
