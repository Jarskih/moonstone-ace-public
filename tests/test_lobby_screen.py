"""Host test of the lobby = the knight screen (docs/NETPLAY.md 4.2b): engine/netroom.cpp (seats, picks, races, the START rule, the wire form of the
room) and game/scene_lobby.cpp (selectors in the seat colours, picks, name entry, controller line, START, the host-only mode line, WAITING FOR HOST).

tests/lobby_driver.cpp is compiled with clang++ (-fno-exceptions -fno-rtti) against the engine and game sources and runs the scenarios on a recording
MenuOps / LobbyOps and a fake LobbySeam.  The network side (LOBBY_PICK / LOBBY_STATE over real byte pipes, 2 to 4 nodes, races, a player leaving,
CONFIG -> identical knight records) is tests/netsession_driver.cpp (tests/test_netsession.py); the wire format is tests/test_netproto.py.
The mutation test proves the driver has teeth: each deliberate mutation of the sources must make it fail.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = 'tests/lobby_driver.cpp'
SOURCES = ['src/engine/netroom.cpp', 'src/engine/netproto.cpp', 'src/engine/netplay.cpp', 'src/engine/pad.cpp', 'src/engine/input.cpp',
           'src/game/scene_lobby.cpp', 'src/game/net_menu.cpp', 'src/engine/inifile.cpp']


def build(out_dir, overrides=None):
    """Compile the driver; overrides = {source path: replacement text} for the mutation test."""
    exe = os.path.join(out_dir, 'lobby.exe')
    cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-Wno-missing-field-initializers',
           '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include'), '-o', exe, os.path.join(ROOT, DRIVER)]
    for s in SOURCES:
        if overrides and s in overrides:
            p = os.path.join(out_dir, s.replace('/', '_'))
            with open(p, 'w', encoding='utf-8', newline='') as fh:
                fh.write(overrides[s])
            cmd.append(p)
        else:
            cmd.append(os.path.join(ROOT, s))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    return exe


def src(path):
    with open(os.path.join(ROOT, path), encoding='utf-8', newline='') as fh:
        return fh.read()


@unittest.skipUnless(CXX, 'needs clang++')
class LobbyScreen(unittest.TestCase):
    def test_room_and_screen(self):
        tmp = tempfile.mkdtemp(prefix='lobby_')
        try:
            r = subprocess.run([build(tmp)], capture_output=True, text=True, timeout=300)
            self.assertEqual(r.returncode, 0, r.stdout[-3000:] + r.stderr[-1000:])
            self.assertIn('OK ', r.stdout)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_mutations_are_caught(self):
        mutations = [
            # the screen: leaving does not restore the title picture (black menu background after the lobby)
            ('src/game/scene_lobby.cpp', 'if(ops.pfnRestoreBackground) {', 'if(false) {'),
            # the room: a held portrait can be claimed by a second player
            ('src/engine/netroom.cpp', 'if(roomIsHeld(r, p.ubSel, ubSeat)) {   // the race is lost: back to browsing, on a free portrait', 'if(false) {'),
            # the room: browsing onto a held portrait is allowed to stay
            ('src/engine/netroom.cpp', 's.ubSel = roomIsHeld(r, p.ubSel, ubSeat) ? roomFirstFree(r, p.ubSel, ubSeat) : p.ubSel;', 's.ubSel = p.ubSel;'),
            # the room: a done pick needs no claim
            ('src/engine/netroom.cpp', 'if(s.ubState != NET_SEAT_CLAIM || s.ubSel != p.ubSel || p.ubPad >= PAD_SELECTABLE_COUNT || p.acName[0] == 0) {', 'if(false) {'),
            # the room: the selector does not skip held portraits
            ('src/engine/netroom.cpp', 'if(!roomIsHeld(r, static_cast<uint8_t>(sw), ubSeat)) {', 'if(!roomIsHeld(r, static_cast<uint8_t>(sw), ubSeat) || sw >= 0) {'),
            # the room: START without another player
            ('src/engine/netroom.cpp', 'return roomJoined(r) >= 2 && roomAllDone(r);', 'return roomAllDone(r);'),
            # the room: the host can leave its own room
            ('src/engine/netroom.cpp', 'if(ubSeat == 0 || ubSeat >= ROOM_SEATS) {', 'if(ubSeat >= ROOM_SEATS) {'),
            # the room: the nodes of the game are the seat numbers
            ('src/engine/netroom.cpp', 'aubSeatOfNode[ubNodes++] = i;', 'aubSeatOfNode[i] = i; ++ubNodes;'),
            # the room: a joiner starts on portrait 0 like the host
            ('src/engine/netroom.cpp', 's.ubSel = roomFirstUnused(r, ubSeat);', 's.ubSel = 0;'),
            # the room: a joiner ignores the selectors of the others
            ('src/engine/netroom.cpp', 'if(!isStoodOn(r, ubPortrait, ubSeat)) {\n			return ubPortrait;', 'if(!isStoodOn(r, ubPortrait, ubSeat) || ubPortrait == 0) {\n			return ubPortrait;'),
            # the room: a pick does not move the others who browse that knight
            ('src/engine/netroom.cpp', 'if(i != ubSeat && o.ubState == NET_SEAT_BROWSE && o.ubSel == p.ubSel) {', 'if(false) {'),
            # the screen: browsing players get a name in the corner too
            ('src/game/scene_lobby.cpp', 'if(seat.ubState < NET_SEAT_CLAIM) {\n			continue;', 'if(seat.ubState < NET_SEAT_BROWSE) {\n			continue;'),
            # the screen: the highlight frame is lost once the knight is picked
            ('src/game/scene_lobby.cpp', '	ops.pfnDrawSprite(MENUBANK_SPRITES, 1, kPortraitX[ubSel & 3], LOBBY_PORTRAIT_Y);', '	if(ePhase == LP_BROWSE) ops.pfnDrawSprite(MENUBANK_SPRITES, 1, kPortraitX[ubSel & 3], LOBBY_PORTRAIT_Y);'),
            # the screen: the name is not cut to the portrait
            ('src/game/scene_lobby.cpp', 'while(n > 0 && lops.pfnSmallWidth(reinterpret_cast<const uint8_t *>(acOut)) > uwMax) {', 'while(n > 0 && lops.pfnSmallWidth(reinterpret_cast<const uint8_t *>(acOut)) > uwMax + 1000) {'),
            # the screen: a knight picked by somebody else under the own selector is not given up
            ('src/game/scene_lobby.cpp', '			ubSel = roomFirstFree(view.room, me().ubSel, view.ubSeat);\n			if(ubSel != me().ubSel) {', '			ubSel = static_cast<uint8_t>(ubSel + 0);\n			if(ubSel != me().ubSel) {'),
            # the screen: START is never greyed
            ('src/game/scene_lobby.cpp', 'if(!roomStartEnabled(room)) {\n			lops.pfnRecolour', 'if(false) {\n			lops.pfnRecolour'),
            # the screen: START works while somebody has not picked
            ('src/game/scene_lobby.cpp', 'if(isHost && isFire && roomStartEnabled(view.room) && seam.pfnStart()) {', 'if(isHost && isFire && seam.pfnStart()) {'),
            # the screen: a client gets a MODE row and a START row too
            ('src/game/scene_lobby.cpp', 'if(isHost && (uwJoy & 8) && eFocus != LF_MODE) {', 'if((uwJoy & 8) && eFocus != LF_MODE) {'),
            # the screen: the mode switches again and again while the stick is held
            ('src/game/scene_lobby.cpp', 'if(isHost && (isHorizEdge || isFire)) {', 'if(isHost && (isHoriz || isFire || isHorizEdge)) {'),
            # the screen: co-op skips the controller line
            ('src/game/scene_lobby.cpp', 'if(view.room.ubMode == NET_MODE_COOP) {           // co-op: the controller next', 'if(false) {'),
            # the screen: the name entry starts empty instead of pre-filled
            ('src/game/scene_lobby.cpp', 'while(szJoin[n] && n < ROOM_NAME_SHOWN) {', 'while(false) {'),
            # the screen: a lost race is not noticed
            ('src/game/scene_lobby.cpp', '		} else if(roomIsHeld(view.room, ubSel, view.ubSeat)) {\n			ePhase = LP_BROWSE;                                    // lost the race', '		} else if(false) {\n			ePhase = LP_BROWSE;                                    // lost the race'),
            # the screen: a stale room counts as a rejected claim
            ('src/game/scene_lobby.cpp', '		} else if(roomIsHeld(view.room, ubSel, view.ubSeat)) {\n			ePhase = LP_BROWSE;                                    // lost the race', '		} else {\n			ePhase = LP_BROWSE;                                    // lost the race'),
            # the screen: Esc during the name entry leaves the lobby
            ('src/game/scene_lobby.cpp', '		if(ePhase == LP_BROWSE) {\n			eResult = LOBBY_LEFT;', '		if(true) {\n			eResult = LOBBY_LEFT;'),
            # the screen: a lost link does not end the lobby
            ('src/game/scene_lobby.cpp', '	if(view.isLost) {\n		eResult = LOBBY_LOST;\n		return true;\n	}', '	if(false) {\n		return true;\n	}'),
            # the screen: names longer than the original editor allows
            ('src/game/scene_lobby.cpp', 'if(ubNameLen >= ROOM_NAME_SHOWN) {', 'if(false) {'),
        ]
        for path, old, new in mutations:
            text = src(path)
            self.assertIn(old, text, 'mutation target vanished: ' + old)
            tmp = tempfile.mkdtemp(prefix='lobby_mut_')
            try:
                exe = build(tmp, {path: text.replace(old, new, 1)})
                r = subprocess.run([exe], capture_output=True, text=True, timeout=300)
                self.assertNotEqual(r.returncode, 0, 'mutation not caught: ' + old)
            except RuntimeError as e:   # a mutation that does not even compile is not a test of the driver
                self.fail('mutation does not compile: %s\n%s' % (old, e))
            finally:
                shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
