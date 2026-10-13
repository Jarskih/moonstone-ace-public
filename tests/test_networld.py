"""Host test of the world snapshot of a rejoin (ROADMAP M12, docs/NETPLAY.md 4.5): engine/netsnap.hpp (byte writer / reader, chunks, the receiving
side), game/net_world.cpp (the world image, its byte form, its hash, pointers made address independent) and game/net_rejoin.cpp (what a returning
player does to records, bindings and the party).

tests/networld_driver.cpp is compiled with clang++ (-fno-exceptions -fno-rtti) and run once.  The mutation test proves the driver has teeth: each
deliberate mutation of the sources must make it fail.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = 'tests/networld_driver.cpp'
SOURCES = ['src/game/net_world.cpp', 'src/game/net_hash.cpp', 'src/game/net_rejoin.cpp', 'src/game/net_drop.cpp', 'src/game/party.cpp', 'src/engine/netplay.cpp',
           'src/engine/pad.cpp']


def build(out_dir, overrides=None):
    exe = os.path.join(out_dir, 'networld.exe')
    cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-D_CRT_SECURE_NO_WARNINGS', '-Wno-missing-field-initializers', '-fno-exceptions', '-fno-rtti',
           '-I', os.path.join(ROOT, 'include'), '-o', exe, os.path.join(ROOT, DRIVER)]
    for s in SOURCES:
        if overrides and s in overrides:
            p = os.path.join(out_dir, s.replace('/', '_'))
            with open(p, 'w', encoding='utf-8') as fh:
                fh.write(overrides[s])
            cmd.append(p)
        else:
            cmd.append(os.path.join(ROOT, s))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    return exe


def read(path):
    with open(os.path.join(ROOT, path), encoding='utf-8') as fh:
        return fh.read()


@unittest.skipUnless(CXX, 'needs clang++')
class NetWorld(unittest.TestCase):
    def test_snapshot_codec_hash_and_rejoin_effects(self):
        tmp = tempfile.mkdtemp(prefix='networld_')
        try:
            r = subprocess.run([build(tmp)], capture_output=True, text=True, timeout=300)
            self.assertEqual(r.returncode, 0, r.stdout[-3000:] + r.stderr[-1000:])
            self.assertIn('OK ', r.stdout)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_every_extra_has_a_live_cell(self):
        """Every NX_ id of the header is read and written by src/rt/net_world.cpp (an id without a cell is 0 on the host as well), and the cells the
        map AI reads at the start of a turn are among them (the first desync of a real rejoin: LAB_0656, the stale move word)."""
        header = read('include/game/net_world.hpp')
        body = header[header.index('enum NetExtra'):header.index('NX_COUNT')]
        ids = re.findall(r'\bNX_[A-Z0-9_]+', body)
        rt = read('src/rt/net_world.cpp')
        market = {'NX_MARKET%d' % i for i in range(6)}
        missing = [i for i in ids if i not in market and not re.search(r'case ' + i + r'\b', rt)]
        self.assertEqual(missing, [], 'extras without a cell in rt/net_world.cpp cellOf')
        for cell, extra in (('mogAiMove', 'NX_AI_MOVE'), ('mogAiWalk[1', 'NX_AI_WALK1'), ('mogRoamLair', 'NX_ROAM_LAIR'), ('mogShopItem', 'NX_SHOP_ITEM'),
                            ('mogAiOpponentPtr', 'NX_OPP_PTR0'), ('mogAiOpponentDist', 'NX_OPP_DIST0'), ('mogStepGate', 'NX_STEP_GATE')):
            self.assertRegex(rt, r'case ' + extra + r'\b[^\n]*\n?[^\n]*&' + re.escape(cell), extra + ' must map ' + cell)

    def test_mutations_are_caught(self):
        mutations = [
            # the receiver keeps the host's addresses instead of its own
            ('src/game/net_world.cpp', 'return ulBase != 0 ? ulBase + ulOff : ulKeep;', 'return ulNorm;'),
            # an unresolvable pointer is zeroed instead of left alone
            ('src/game/net_world.cpp', 'return ulBase != 0 ? ulBase + ulOff : ulKeep;', 'return ulBase != 0 ? ulBase + ulOff : 0;'),
            # pointers into the data regions are not recognised
            ('src/game/net_world.cpp', 'return tagged(TAG_REGION + i, ulPtr - im.aulRegionBase[i]);', 'return tagged(TAG_OTHER, 0);'),
            # name pointers are not recognised
            ('src/game/net_world.cpp', 'return tagged(TAG_NAME, i);', 'return tagged(TAG_OTHER, 0);'),
            # the pool is not recognised
            ('src/game/net_world.cpp', 'return tagged(TAG_POOL, ulPtr - im.ulPoolBase);', 'return tagged(TAG_OTHER, 0);'),
            # the receiver's own creatures are left active
            ('src/game/net_world.cpp', '			s.aCreatures[i].ulActive = 0;\n', '			;\n'),
            # the creature pool is not sent / not hashed
            ('src/game/net_world.cpp', '	walkCreatures(c, s);\n', ''),
            # a creature slot out of range is accepted (the count check is belt and braces: a bad count also runs into the slot and length checks)
            ('src/game/net_world.cpp', '		if(ubSlot >= NETHASH_CREATURES) {', '		if(false) {'),
            # a creature record is forgotten
            ('src/game/net_world.cpp', '		walkKnight(c, s.aCreatures[ubSlot]);\n', ''),
            # a field of the record is forgotten
            ('src/game/net_world.cpp', '	c.v(k.uwGold);\n', ''),
            ('src/game/net_world.cpp', '	c.v(k.ubTeam);   // the union with ubTargetSlot: one byte\n', ''),
            # the extras are forgotten
            ('src/game/net_world.cpp', '		if(netExtraIsPtr(i)) {\n			c.ptr(im.aulX[i]);\n		} else {\n			c.v(im.aulX[i]);\n		}', '		;'),
            # the roam lair / opponent pointers of the map AI are sent as plain numbers (the host's addresses)
            ('src/game/net_world.cpp', 'ubExtra == NX_ACTIVE_CURRENT || ubExtra == NX_ROAM_LAIR ||', 'ubExtra == NX_ACTIVE_CURRENT ||'),
            # the lair loot is forgotten
            ('src/game/net_world.cpp', '		walkInventory(c, s.aLoot[i]);\n', ''),
            # the pool is forgotten
            ('src/game/net_world.cpp', '		walkKnight(c, im.aPool[i]);\n', ''),
            # the party is forgotten
            ('src/game/net_world.cpp', '	c.flag(s.party.isNet);\n', ''),
            # trailing garbage is accepted
            ('src/game/net_world.cpp', 'if(!r.isOk || r.ulPos != ulLen || im.st.party.n > PARTY_MAX) {', 'if(!r.isOk || im.st.party.n > PARTY_MAX) {'),
            # the receiver accepts any extra count
            ('src/game/net_world.cpp', 'snapGet8(r) != NX_COUNT ||', 'false ||'),
            # the rejoin effects
            ('src/game/net_rejoin.cpp', 'aRecords[k].ulKind = aubPortrait[k & (NET_NODE_MAX - 1)];', 'aRecords[k].ulKind = aRecords[k].ulKind;'),
            ('src/game/net_rejoin.cpp', '		aBind[k] = aOrig[k];', '		;'),
            ('src/game/net_rejoin.cpp', '	p.isNet = true;', '	;'),
            ('src/game/net_rejoin.cpp', '		p.aubNode[m] = ubNode;', '		;'),
            ('src/game/net_rejoin.cpp', 'p.aubPad[m] = aOrig[m].ubPad;', ';'),
            ('src/game/net_rejoin.cpp', 'q.ubApplied = static_cast<uint8_t>(q.ubApplied & ~(1U << ubNode));', ';'),
            # a snapshot's receiver
            ('include/engine/netsnap.hpp', 'ulCrcAtEnd == rx.ulCrc &&\n		crc32(rx.aubBuf, rx.ulBytes) == rx.ulCrc;', 'ulCrcAtEnd == rx.ulCrc;'),
            ('include/engine/netsnap.hpp', 'uwIndex != rx.uwNext ||', 'false ||'),
            ('include/engine/netsnap.hpp', 'uwLen != snapChunkLen(rx.ulBytes, uwIndex)', 'false'),
        ]
        for path, old, new in mutations:
            text = read(path)
            self.assertIn(old, text, 'mutation target vanished: ' + old)
            tmp = tempfile.mkdtemp(prefix='networld_mut_')
            try:
                if path.startswith('include/'):
                    inc = os.path.join(tmp, 'include')
                    shutil.copytree(os.path.join(ROOT, 'include'), inc)
                    with open(os.path.join(tmp, path), 'w', encoding='utf-8') as fh:
                        fh.write(text.replace(old, new, 1))
                    exe = os.path.join(tmp, 'networld.exe')
                    cmd = [CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-Wno-missing-field-initializers', '-fno-exceptions', '-fno-rtti', '-I', inc,
                           '-o', exe, os.path.join(ROOT, DRIVER)] + [os.path.join(ROOT, s) for s in SOURCES]
                    r = subprocess.run(cmd, capture_output=True, text=True)
                    self.assertEqual(r.returncode, 0, r.stderr[-1500:])
                else:
                    exe = build(tmp, {path: text.replace(old, new, 1)})
                r = subprocess.run([exe], capture_output=True, text=True, timeout=300)
                self.assertNotEqual(r.returncode, 0, 'mutation not caught: ' + old)
            finally:
                shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
