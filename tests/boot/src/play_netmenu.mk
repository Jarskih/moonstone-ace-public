# Multiplayer screen (ROADMAP 12.8a, tests/test_net_menu.py is the logic test) and the lobby = the knight screen (NETPLAY.md 4.2b, tests/test_lobby_screen.py
# is the logic test): the main menu's MULTIPLAYER row opens the screen, the name and the address are edited, the join page shows every status (no stack,
# connecting, no answer, wrong version, connected), the fake host seats the joining player (poke netseated) and the LOBBY opens as a client, the host's
# start message (poke netstarted) ends it; then HOST GAME goes straight into the lobby with faked players (poke netfake / netpeers / netjoin /
# netseated / netstarted, rt/net_stub.cpp): the host picks a portrait, the name, the controller, START leaves the lobby, Esc goes back through the
# Multiplayer screen to the title menu, and a normal game still starts from LOCAL (Select Knight -> knight -> name -> map).
# The lobby shots (play-netmenu-lobby-*) are new in the lobby flow: no reference yet, the integrator blesses them (docs/AUTOPLAY.md).
# net.ini is written to PROGDIR (name, address, port): the backspaces clear whatever an earlier run saved.
# shotcmp-tol play-netmenu-title 0.5
# shotcmp-tol play-netmenu-title-multi 0.5
# shotcmp-tol play-netmenu-main 0.5
# shotcmp-tol play-netmenu-name 0.5
# shotcmp-tol play-netmenu-join 0.5
# shotcmp-tol play-netmenu-join-address 0.5
# shotcmp-tol play-netmenu-join-nostack 0.5
# shotcmp-tol play-netmenu-join-wait 0.5
# shotcmp-tol play-netmenu-join-noanswer 0.5
# shotcmp-tol play-netmenu-join-version 0.5
# shotcmp-tol play-netmenu-join-connected 0.5
# shotcmp-tol play-netmenu-title-started 0.5
# shotcmp-tol play-netmenu-lobby-client 0.5
# shotcmp-tol play-netmenu-lobby-empty 0.5
# shotcmp-tol play-netmenu-lobby-1 0.5
# shotcmp-tol play-netmenu-lobby-3 0.5
# shotcmp-tol play-netmenu-lobby-name 0.5
# shotcmp-tol play-netmenu-lobby-control 0.5
# shotcmp-tol play-netmenu-lobby-control2 0.5
# shotcmp-tol play-netmenu-lobby-picked 0.5
# shotcmp-tol play-netmenu-lobby-start 0.5
# shotcmp-tol play-netmenu-lobby-again 0.5
# shotcmp-tol play-netmenu-back-main 0.5
# shotcmp-tol play-netmenu-title-back 0.5
# shotcmp-tol play-netmenu-knight 1.8
# shotcmp-tol play-netmenu-name-entry 1.5
# shotcmp-tol play-netmenu-map 1.0
# shotdiff play-netmenu-title play-netmenu-title-multi 0.2
# shotdiff play-netmenu-main play-netmenu-name 0.2
# shotdiff play-netmenu-join-nostack play-netmenu-join-wait 0.2
# shotdiff play-netmenu-join-wait play-netmenu-join-noanswer 0.2
# shotdiff play-netmenu-lobby-empty play-netmenu-lobby-1 0.2
# shotdiff play-netmenu-lobby-1 play-netmenu-lobby-3 0.2
# shotdiff play-netmenu-lobby-3 play-netmenu-lobby-name 0.2
# shotdiff play-netmenu-lobby-control play-netmenu-lobby-control2 0.2
wait file mindscape max 900
frame 20 key SPACE tap
wait input 10 max 3000
+60 shot play-netmenu-title
# the main menu (owner 2026-10-11): down to the MULTIPLAYER row, fire: the Multiplayer screen (a 1-frame pulse is held until
# the menu reads it, then released at once: one row, never two)
+10 joy1 down pulse 1
+160 shot play-netmenu-title-multi
+10 joy1 fire pulse
+150 shot play-netmenu-main
# NAME (HOST, JOIN, FIND, NAME, PUBLIC, BACK): down x3, clear it (16 backspaces), type BOB, Return = next row (PUBLIC)
+10 joy1 down pulse
+30 joy1 down pulse
+30 joy1 down pulse
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+30 type BOB
+100 shot play-netmenu-name
+10 key RETURN tap
# PUBLIC -> up x3 (NAME, FIND) = JOIN GAME, fire: the join page (cursor on ADDRESS)
+30 joy1 up pulse
+30 joy1 up pulse
+30 joy1 up pulse
+30 joy1 fire pulse
+90 shot play-netmenu-join
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+20 key BACKSPACE tap
+30 type 192
+100 key PERIOD tap
+30 type 168
+100 key PERIOD tap
+30 type 0
+40 key PERIOD tap
+30 type 7
+40 shot play-netmenu-join-address
+10 key RETURN tap
+30 key RETURN tap
# CONNECT without a stack
+30 joy1 fire pulse
+90 shot play-netmenu-join-nostack
+10 poke netfake 1
+1 poke netjoin 3
+10 joy1 fire pulse
+90 shot play-netmenu-join-wait
+10 poke netjoin 1
+90 shot play-netmenu-join-noanswer
+10 poke netjoin 2
+10 joy1 fire pulse
+90 shot play-netmenu-join-version
+10 poke netjoin 0
+10 joy1 fire pulse
+90 shot play-netmenu-join-connected
# the fake host seats us: the lobby opens as a client (the host has picked portrait 0; WAITING FOR HOST where START is)
+10 poke netseated 1
+200 shot play-netmenu-lobby-client
# the host's start message: the lobby ends, the (fake) session does not begin, back to the title menu
+10 poke netstarted 1
+200 shot play-netmenu-title-started
# the host side: Multiplayer, HOST GAME = straight into the lobby (the knight screen)
+10 joy1 fire pulse
+150 shot play-netmenu-main-saved
+10 joy1 fire pulse
+150 shot play-netmenu-lobby-empty
+10 poke netpeers 1
+60 shot play-netmenu-lobby-1
+10 poke netpeers 3
+60 shot play-netmenu-lobby-3
# the host's own pick: the selector stands on portrait 0 (the fake players hold 3, 2 and 1); fire, the name (fire keeps it), the controller
# (right = Joystick 2, fire takes it); START is enabled when all four have picked
+10 joy1 fire pulse 8
+60 shot play-netmenu-lobby-name
+10 joy1 fire pulse 8
+60 shot play-netmenu-lobby-control
+10 joy1 right pulse 4
+30 shot play-netmenu-lobby-control2
+10 joy1 fire pulse 8
+60 shot play-netmenu-lobby-picked
# START: down to it, fire; the fake session does not begin: back at the title menu
+10 joy1 down pulse 4
+30 shot play-netmenu-lobby-start
+10 joy1 fire pulse 8
+300 shot play-netmenu-title-host
# again: HOST GAME, then Esc leaves the lobby (the Multiplayer screen again), Esc leaves that too
+10 poke netpeers 0
+10 joy1 fire pulse
+150 joy1 fire pulse
+150 shot play-netmenu-lobby-again
+10 key ESC tap
+150 shot play-netmenu-back-main
+10 key ESC tap
+200 shot play-netmenu-title-back
# a normal game: up = LOCAL, fire, down x3 = Select Knight (Mode and the empty line in between), the knight screen, the name, the map
+10 joy1 up pulse
+30 joy1 fire pulse
+60 joy1 down pulse
+30 joy1 down pulse
+30 joy1 down pulse
+50 joy1 fire pulse
+400 shot play-netmenu-knight
+10 joy1 fire pulse
+300 shot play-netmenu-name-entry
+10 joy1 fire pulse
+600 shot play-netmenu-map
+10 quit
