# Settings screen (ROADMAP 9.9, docs/MODDING.md "4-O") end to end: the main menu (LOCAL / MULTIPLAYER / SETTINGS, owner 2026-10-11, V0.2 stays visible)
# and the title menu behind LOCAL in the classic and the co-op configuration.  Settings (opened by `poke menuact 2`): the list shows tough_dragon only
# (the packs that only set the keys of the two settings are hidden), the settings show the values in effect (DEXTERITY OFF, MONSTER SCALING FULL);
# tough_dragon ON, DEXTERITY ON, MONSTER SCALING NONE (right from FULL wraps), BACK; the main menu is redrawn cleanly, the screen is opened again
# (the choice came back from PROGDIR:mods/options.ini), Esc leaves.  The pack-versus-pack auto switch-off needs two packs that set the same key and is
# covered by the host test (tests/test_options_menu.py).
# A stick step is `pulse 10`: the Settings and title menus read once per 10-frame redraw wait.
# The serial log carries the mods.log lines of the rebuild (`pack <name>`, files applied, final values) and `options: screen left, saved`.
# Run: MS_BOOT_INSTANCE=98 py tools/integrate.py --boot --only-boot --no-play --no-rel --boot-script tests/boot/play_options.txt
# shotcmp-tol play-options-title 5
# shotcmp-tol play-options-open 5
# shotcmp-tol play-options-pack 5
# shotcmp-tol play-options-settings 5
# shotcmp-tol play-options-title-after 5
# shotcmp-tol play-options-reopen 5
# shotcmp-tol play-options-title-end 5
# shotcmp-tol play-options-title-local 5
# shotcmp-tol play-options-title-coop 5
wait file mindscape max 900
frame 20 key SPACE tap
wait input 10 max 3000
+60 shot play-options-title
# the Settings row by poke (a stick pulse on the menu was missed twice in the first run: no cursor counting)
+10 poke menuact 2
wait log screen_open max 1500
+60 shot play-options-open
# fire = tough_dragon ON
+10 joy1 fire pulse 10
+40 shot play-options-pack
# down = DEXTERITY, right = ON; down = MONSTER SCALING, right = NONE (wraps from FULL); down = BACK
+10 joy1 down pulse 10
+40 joy1 right pulse 10
+40 joy1 down pulse 10
+40 joy1 right pulse 10
+40 shot play-options-settings
+10 joy1 down pulse 10
+40 joy1 fire pulse 10
wait log screen_left max 3000
+100 shot play-options-title-after
# open it again: the choices are still there (read back from options.ini); Esc leaves
+10 joy1 fire pulse 10
wait log screen_open max 1500
+100 shot play-options-reopen
+10 key ESC tap
wait log screen_left max 3000
+100 shot play-options-title-end
# Settings -> up x2 = LOCAL (the cursor stayed on Settings), fire: the title menu (classic); down = Mode (under Players), fire = Coop: the co-op configuration
+10 joy1 up pulse 10
+40 joy1 up pulse 10
+40 joy1 fire pulse 10
+60 shot play-options-title-local
+10 joy1 down pulse 10
+40 joy1 fire pulse 10
+60 shot play-options-title-coop
+10 quit
