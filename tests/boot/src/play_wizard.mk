# Math the wizard (node 8, id $1E): three text pages (each needs fire), the status screen, back on the map.
# The gift is random (gold / a stat / an item / the frog curse): the cave and vanish pages vary, hence the loose tolerances.
# shotcmp-tol play-wiz-cave 10
# shotcmp-tol play-wiz-vanish 10
# shotcmp-tol play-wiz-status 5
# shotcmp-tol play-wiz-tower 3
@PRE
@SET 1630
+0 poke warp_node 8
+80 joy1 fire pulse 8
+500 shot play-wiz-tower
+10 joy1 fire pulse 8
+300 shot play-wiz-cave
+10 joy1 fire pulse 8
+300 shot play-wiz-vanish
+10 joy1 fire pulse 8
+300 shot play-wiz-status
+10 @click 236 150
+400 shot play-wiz-map
+100 quit
