Moonstone ACE @VERSION@ - Amiga release
=======================================

Moonstone: A Hard Days Knight (Mindscape, 1991) rebuilt in C++ on ACE for the Amiga 1200
(68020, AGA, Kickstart 3.x).  It plays like the original and adds network play and mods.

THIS PACKAGE CONTAINS NO GAME DATA.  You need your own copy of the three original Moonstone
disks (as .adf images, or the real floppies).  The game reads its data from them every time
it starts; nothing is copied or converted.


What you need
-------------
  - an Amiga 1200 (68020, AGA, Kickstart 3.x, 2 MB chip RAM; fast RAM is a bonus) or WinUAE
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
  - Memory: with fast RAM the game keeps about 0.6 MB of chip RAM; with none it needs about
    1 MB of free chip RAM.  On a stock 2 MB machine start it from a Shell or the
    startup-sequence rather than from a crowded Workbench.
  - The game writes small log files (boot.log, files.log, crash.log, mods.log) into its drawer.


Network play (new in this version)
----------------------------------
Two to four players, over a LAN or the internet, in the classic game and in co-op.  All
machines run the same game and only the controllers' input travels, so every machine must
run exactly this version with the same three disks.

You need a TCP/IP stack that provides bsdsocket.library: Roadshow, AmiTCP, Miami or Genesis
started before the game, or WinUAE with "bsdsocket.library emulation" on (the supplied
WinUAE config has it on).  Nothing else is installed.

  - On the title menu choose MULTIPLAYER.
  - HOST GAME opens a lobby (the knight-select screen); the host presses START when the
    others have joined and everybody has picked a knight.
  - CONNECT: type the host's address.  FIND GAME lists public games from the lobby server.
  - Ports: a direct game uses port 6502, TCP and UDP.  To host over the internet forward
    6502 TCP and 6502 UDP to your machine.  UDP carries the input with a history (so lost
    packets cost nothing); if it does not get through the game falls back to TCP by itself.
  - The lobby / relay server: moonstone.shatteredallegiance.com, TCP port 6510 (and UDP 6510
    for the relay).  A public game is listed there; if you cannot be reached directly
    (router, firewall) the server relays the game, so nobody has to forward a port.
  - A dropped player is replaced by the computer (classic) or by the host's controller
    (co-op) and can REJOIN from the same menu.
  - A network game shows a ping / loss line; F10 switches it off and on.

PROGDIR:net.ini  (written by the game; you may edit it)
    [net]
    name = "PLAYER"            your name in the lobby
    address = "192.168.1.20"   the last address you connected to
    port = 6502                the game port for direct play
    public = yes               list a hosted game on the lobby server (yes / no)
    lobby = "name"             another lobby server (default moonstone.shatteredallegiance.com)
    lobby_port = 6510          its port
    udp = yes                  no = TCP only
    stats = yes                no = hide the ping / loss line

Command line / Tool types (optional):
    moonstone HOST                   host straight away
    moonstone JOIN 192.168.1.20      connect straight away      (also PORT n, NAME x, DELAY n, TCP)


Mods
----
Rule tweaks need no rebuild.  Copy an .ini file into the  mods  drawer next to the game.
  mods/defaults/   every setting with its default value (reference: edit a copy)
  mods_examples/   example mods; copy the .ini files of one into  mods/
A broken file is skipped with a line-numbered message in mods.log.  See docs/MODDING.md and
docs/MOD_KEYS.md.


WinUAE
------
moonstone-ace-a1200.uae is an A1200 (68020, AGA, 2 MB chip, 8 MB fast RAM, bsdsocket on)
that mounts the "Moonstone" drawer as DH0: and boots into the game.
  1. Put your three .adf files into  Moonstone\disks\ .
  2. Run  start-winuae.bat  (set the WINUAE variable if WinUAE is not in
     C:\Program Files\WinUAE, and KICKSTART to your Kickstart 3.1 A1200 ROM file), or open
     the .uae in WinUAE and fix the two paths (kickstart_rom_file, filesystem2) in the GUI.
  3. Arrow keys + Right Ctrl are joystick port 1.
For two windows on one PC (network test) keep win32.inactive_pause=false, as the config does.
WinUAE and the Kickstart ROM are not part of this package.


Integrity
---------
SHA256SUMS.txt lists the SHA-256 of every file in this archive.  The archive holds no file
that comes from the original disks (the packaging refuses to build if one does).

Source, docs and issues: https://github.com/Jarskih/moonstone-ace-public
