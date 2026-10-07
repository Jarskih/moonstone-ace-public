# The map: the knight walks (port 1: right, then down), the status screen (Space), the end of the turn (E): the black knights
# move, the day changes ("Next Day", fire: the autopilot presses it), and the knight's turn comes round again. A dump of the map
# (knights, places, lairs) goes to the serial log. Walking costs turn points: stay short, the turn ends when they are spent.
# the map's colour-cycled movement/region outlines vary 0.1-0.6 % with timing
# shotcmp-tol play-map-start 1.0
# shotcmp-tol play-map-walked 1.0
# shotcmp-tol play-map-after-status 1.0
# shotcmp-tol play-map-ai-moves 3
# shotcmp-tol play-map-next-day 3
# shotcmp-tol play-map-next-turn 3
# shotcmp-tol play-map-arrival-menu 3
# shotcmp-tol play-map-arrival-village 3
# shotcmp-tol play-map-end 3
@PRE
@SET 1630
+0 shot play-map-start
+10 poke dump 1
+30 @hold joy1 right 40
+10 @hold joy1 down 20
+30 shot play-map-walked
+10 key SPACE tap
+300 shot play-map-status
+10 @click 236 150
+300 shot play-map-after-status
+10 key E tap
wait var turn 1 max 4000
+100 shot play-map-ai-moves
wait var turn 0 max 20000
+100 shot play-map-next-day
+10 joy1 fire pulse 8
+300 shot play-map-next-turn

# the knight is standing on its home village and a lair: fire opens the arrival menu, key 1 enters the village
+10 joy1 fire pulse 8
+100 shot play-map-arrival-menu
+10 key 1 tap
+300 shot play-map-arrival-village
+10 @click 236 150
+300 shot play-map-end
+10 poke dump 1
+100 quit
