# The home village of the knight (node 0, id $15): a castle visit gives a life, then the status screen (left by the right pillar)
@PRE
@SET 1630
+0 poke lives 1
+10 poke warp_node 0
+70 joy1 fire pulse 8
+300 shot play-castle-status
+10 @click 236 150
+400 shot play-castle-map
+100 quit
