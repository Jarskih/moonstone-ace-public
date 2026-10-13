Moonstone ACE @VERSION@ @PREVIEW_TAG@- Windows
=============================================
@PREVIEW_BLOCK@
Moonstone: A Hard Days Knight (Mindscape, 1991) rebuilt in C++.  This is the PC (SDL3)
version of the game that also runs on the Amiga 1200.

THIS PACKAGE CONTAINS NO GAME DATA.  You need your own copy of the three original Moonstone
disks as .adf images.


Start
-----
  1. Put the three Moonstone disk images (A, B and C; any file names) into the  disks
     folder next to moonstone.exe.
  2. Run moonstone.exe.  Esc or closing the window quits.
The game identifies each disk by its contents and reads the files straight from the images;
they are never changed.  Another folder:  moonstone.exe --disks "D:\my disks"

Needs: Windows 10 or later and the 32-bit Microsoft Visual C++ Redistributable (VC++
2015-2022, x86) if it is not installed already.  SDL3.dll must stay next to the exe.


Controls
--------
  - Joystick port 1: cursor keys + Right Ctrl or Space (fire), or the first game controller
    (d-pad / left stick, A / X fire).  Port 0 (a second human player) is the mouse or a
    second game controller.
  - On the map: Space = status screen, E = end of turn.
  - --skip-intro skips the intro; --joykeys none turns the keyboard joystick off.


Network play
------------
Not in this build.  Network play (lobby, direct and relay, co-op and classic, up to four
players) is in the Amiga version; the PC build joins as a network node in 0.3.0.


Mods
----
mods_examples\ holds the example mods of the Amiga version.  The PC build does not read a
mods folder yet; the files are included so you can see the format.


Integrity
---------
SHA256SUMS.txt lists the SHA-256 of every file in this archive.  SDL3 is under the zlib
licence (SDL3_LICENSE.txt).  The archive holds no file that comes from the original disks
(the packaging refuses to build if one does).

Source, docs and issues: https://github.com/Jarskih/moonstone-ace-public
