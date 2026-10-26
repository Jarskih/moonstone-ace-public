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
  - F11 toggles fullscreen.  F12 toggles an optional CRT look (scanlines, phosphor mask, glow;
    best at a window of 3x or more) and remembers the choice; --crt starts with it on,
    --no-crt forces it off.
  - --skip-intro skips the intro; --joykeys none turns the keyboard joystick off.


Sound
-----
The sound is a model of the Amiga's Paula chip, rendered band-limited (no aliasing) at 48 kHz.
Options (command line; --audio-save remembers the ones you give in audio.txt in your user
folder, %APPDATA%\MoonstoneACE\Moonstone, which you can also edit by hand):
  --audio-separation N   0..100, default 70.  100 = hard Amiga panning (channels 0 and 3 left,
                         1 and 2 right), 0 = mono.  70 is comfortable on headphones.
  --audio-filter F       off, a500, a500led, a1200 (default), a1200led.  The output stage of the
                         machine: a1200 is the A1200 (almost no filtering, it just removes DC),
                         a500 the duller A500; the led variants add the 3 kHz "LED" filter,
                         which the game itself never switches on.
  --audio-render R       blep (default, band-limited) or box (the old plain averaging).
  --audio-save           write the audio settings in force to audio.txt.
If the sound ever crackles, the log (moonstone.log) ends with "underruns" and "least queued":
tell us the numbers.


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
