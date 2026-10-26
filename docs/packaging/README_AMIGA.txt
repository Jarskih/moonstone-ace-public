Moonstone ACE @VERSION@ - Amiga release
=======================================

Moonstone: A Hard Days Knight (Mindscape, 1991) rebuilt in C++ on ACE for the Amiga 1200
(68020, AGA, Kickstart 3.x).  It plays like the original and adds @NET_FEATURE@mods.
@BUILD_KIND@

THIS PACKAGE CONTAINS NO GAME DATA.  You need your own copy of the three original Moonstone
disks (as .adf images, or the real floppies).  The game reads its data from them every time
it starts; nothing is copied or converted.


What you need
-------------
  - an Amiga 1200 (68020, AGA, Kickstart 3.x, @HW_MEMORY@) or WinUAE
    set up as one,
  - a hard disk (or CF card, or a USB stick as a drive on a THEA1200),
  - the three Moonstone disks A, B and C as .adf files.  Any file names work: the game
    recognises each disk by the files inside it.


Install on a hard disk (WHDLoad is not needed)
----------------------------------------------
  1. Copy the whole "Moonstone" drawer of this archive to your Amiga, for example to
     DH0:Moonstone.
  2. Copy your three .adf files into DH0:Moonstone/disks/ .
  3. Double-click "moonstone" (give it any Tool icon first if you want one) or start it from a
     Shell:  DH0:Moonstone/moonstone

If a disk is missing or is not the known version, the game stops with a message that says
which disk it needs.  Leave the game with a reset (Ctrl-Amiga-Amiga); there is no quit yet.


How the game finds the original data
------------------------------------
For every file it needs, the game looks in
  1. the drawer it was started from ("PROGDIR:", also a data/ drawer there, if you copied
     the files off the disks),
  2. the floppy drives DF0: to DF3: (insert disks A, B and C),
  3. last, the disk images: every .adf (exactly 901,120 bytes) in PROGDIR:disks/ and
     PROGDIR: is identified by its contents and used as disk A, B or C.
The images are the intended way: drop them into disks/ and start.  They are only read,
never changed.


Playing
-------
  - Up to four knights play in turns; knights nobody picks are played by the computer.
    "Practice" on the title menu is a single training fight.
  - Joystick port 1 plays; a second human player uses the mouse port (port 0).
  - On the map: move to a place and press fire.  Space = status screen, E = end of turn.
  - Goal: win the keys to the Valley of the Gods and finish the quest at Stonehenge with the
    Moonstones before the other knights do.
@MEMORY_BULLET@  - The game writes small log files (boot.log, files.log, crash.log, mods.log) into its drawer.


@NET_SECTION@Mods
----
Rule tweaks need no rebuild.  Copy an .ini file into the  mods  drawer next to the game.
  mods/defaults/   every setting with its default value (reference: edit a copy)
  mods_examples/   example mods; copy the .ini files of one into  mods/
A broken file is skipped with a line-numbered message in mods.log.  See docs/MODDING.md and
docs/MOD_KEYS.md.


WinUAE
------
moonstone-ace-a1200.uae is an A1200 (68020, AGA, 2 MB chip, @WINUAE_MEMORY@)
that mounts the "Moonstone" drawer as DH0: and boots into the game.
  1. Put your three .adf files into  Moonstone\disks\ .
  2. Run  start-winuae.bat  (set the WINUAE variable if WinUAE is not in
     C:\Program Files\WinUAE, and KICKSTART to your Kickstart 3.1 A1200 ROM file), or open
     the .uae in WinUAE and fix the two paths (kickstart_rom_file, filesystem2) in the GUI.
  3. Arrow keys + Right Ctrl are joystick port 1.
@WINUAE_TWO_WINDOWS@WinUAE and the Kickstart ROM are not part of this package.


Integrity
---------
SHA256SUMS.txt lists the SHA-256 of every file in this archive.  The archive holds no file
that comes from the original disks (the packaging refuses to build if one does).

Source, docs and issues: https://github.com/Jarskih/moonstone-ace-public
