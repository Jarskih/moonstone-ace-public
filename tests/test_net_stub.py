"""Host test for src/rt/net_stub.cpp (the seams of the Multiplayer screen and the lobby: the real session or the autoplay fake) and a source-order check of
the no-stack path of src/rt/net_session.cpp (which only builds for the Amiga).

Two regressions of tests/boot/play_netmenu.txt (2026-10-11, after the relay / rejoin merges) are pinned here:
  * a JOIN without a TCP/IP stack froze the Multiplayer screen (initNode fingerprints the executable before anything asks whether there is a stack)
    and showed no status; the join now asks for the stack first and builds no node without one, so the poll says NO TCP IP STACK at once;
  * a real attempt that began before `poke netfake 1` was never closed (the fake close did not reach the real session), the fake start of the client
    lobby then waited 15 s for a session; closing under the fake now closes the real session too, and netSessionBegin refuses without a stack.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')
SOURCES = ['src/rt/net_stub.cpp', 'src/engine/netroom.cpp', 'src/engine/netproto.cpp', 'src/engine/netplay.cpp']

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "rt/net_stub.hpp"
#include "rt/net_session.hpp"
#include "rt/net_find.hpp"

static int g_checks, g_fails;
#define CHECK(c) do { ++g_checks; if(!(c)) { ++g_fails; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

namespace rt {
static int g_realJoin, g_realClose, g_realPoll, g_realLobbyPoll, g_realHost;
void netSessionHostOptions(bool, const char *, uint16_t) {}
bool netSessionHostStart(uint16_t, const char *) { ++g_realHost; return false; }
void netSessionJoinStart(const char *, uint16_t, const char *) { ++g_realJoin; }
void netSessionJoinRelay(uint32_t, const char *) { ++g_realJoin; }
void netSessionPoll(ms::game::NetPoll &o) { ++g_realPoll; o.eStatus = ms::game::NETST_NO_STACK; o.bSeated = false; }
void netSessionClose() { ++g_realClose; }
void netSessionLobbyPoll(ms::game::LobbyView &o) { ++g_realLobbyPoll; o.isLost = true; }
void netSessionLobbyPick(const ms::NetLobbyPick &) {}
void netSessionLobbyMode(uint8_t) {}
bool netSessionLobbyStart() { return false; }
bool netSessionRejoinAvailable(char (&)[ms::NET_NAME_LEN]) { return false; }
void netSessionRejoinStart(const char *) {}
void netFindList() {}
bool netFindJoinReq(uint8_t) { return false; }
void netFindPoll(ms::game::FindView &) {}
void netFindStop() {}
}

namespace rt { namespace platform { void logFormat(const char *, ...) {} } }

int main() {
	using namespace ms::game;
	const NetSeam &net = rt::netSeam();
	const LobbySeam &lob = rt::lobbySeam();

	// no fake: every seam call is the real session's
	net.pfnJoinStart("192.168.0.7", 6502, "BOB");
	CHECK(rt::g_realJoin == 1);
	NetPoll p;
	net.pfnPoll(p);
	CHECK(rt::g_realPoll == 1 && p.eStatus == NETST_NO_STACK);
	net.pfnClose();
	CHECK(rt::g_realClose == 1);

	// the regression: the real attempt is under way, the fake goes on, the screen closes the link: the REAL session must be closed
	net.pfnJoinStart("192.168.0.7", 6502, "BOB");
	CHECK(rt::g_realJoin == 2);
	CHECK(rt::netFakePoke("netfake", 1));
	const int iBefore = rt::g_realClose;
	net.pfnClose();
	CHECK(rt::g_realClose == iBefore + 1);

	// the fake client: connecting / answers / seated, then the host's start ends the lobby (isStarted), the real lobby is never polled
	CHECK(rt::netFakePoke("netjoin", 3));
	net.pfnJoinStart("192.168.0.7", 6502, "BOB");
	CHECK(rt::g_realJoin == 2);
	net.pfnPoll(p);
	CHECK(p.eStatus == NETST_CONNECTING && !p.bSeated);
	CHECK(rt::netFakePoke("netjoin", 0));
	net.pfnPoll(p);
	CHECK(p.eStatus == NETST_CONNECTED && !p.bSeated);
	CHECK(rt::netFakePoke("netseated", 1));
	net.pfnPoll(p);
	CHECK(p.eStatus == NETST_CONNECTED && p.bSeated);
	LobbyView v;
	memset(&v, 0, sizeof v);
	lob.pfnPoll(v);
	CHECK(!v.isHost && !v.isStarted && !v.isLost && v.ubSeat == 1);
	CHECK(rt::netFakePoke("netstarted", 1));
	lob.pfnPoll(v);
	CHECK(v.isStarted && !v.isLost);
	CHECK(rt::g_realLobbyPoll == 0);

	// the fake host: HOST GAME opens, peers join
	CHECK(rt::netFakePoke("netpeers", 2));
	CHECK(net.pfnHostStart(6502, "BOB") && rt::g_realHost == 0);
	lob.pfnPoll(v);
	CHECK(v.isHost && !v.isStarted);

	if(g_fails) printf("FAILED %d of %d\n", g_fails, g_checks);
	else printf("DONE %d\n", g_checks);
	return g_fails != 0;
}
'''


def run_driver(overrides=None):
    with tempfile.TemporaryDirectory() as tmp:
        p, exe = os.path.join(tmp, 'drv.cpp'), os.path.join(tmp, 'drv.exe')
        with open(p, 'w') as f:
            f.write(DRIVER)
        cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fno-exceptions', '-fno-rtti',
               '-DMS_AUTOPLAY=1', '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include', 'host'), '-I', os.path.join(ROOT, 'include'),
               '-I', os.path.join(ROOT, 'src'), p]
        for s in SOURCES:
            if overrides and s in overrides:
                q = os.path.join(tmp, s.replace('/', '_'))
                with open(q, 'w', encoding='utf-8') as fh:
                    fh.write(overrides[s])
                cmd.append(q)
            else:
                cmd.append(os.path.join(ROOT, s))
        cmd += ['-o', exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr + r.stdout)
        out = subprocess.run([exe], capture_output=True, text=True)
        return out.stdout.splitlines(), out.returncode


def read(path):
    with open(os.path.join(ROOT, path), encoding='utf-8') as fh:
        return fh.read().replace('\r\n', '\n')


def function_body(src, header):
    """The text of the function whose signature line starts with `header` (up to the first column-0 closing brace)."""
    i = src.index(header)
    j = src.index('\n}\n', i)
    return src[i:j]


@unittest.skipUnless(CXX, 'needs clang++')
class NetStubSeams(unittest.TestCase):
    def test_driver_checks(self):
        out, rc = run_driver()
        self.assertTrue(out and out[-1].startswith('DONE') and rc == 0, '\n'.join(out))

    def test_closing_under_the_fake_closes_the_real_session(self):
        src = read('src/rt/net_stub.cpp')
        old = '\tnetSessionClose();   // always:'
        self.assertIn(old, src)
        mutated = src.replace(old, '\t(void)0;   // always:')
        out, rc = run_driver({'src/rt/net_stub.cpp': mutated})
        self.assertTrue(rc != 0 and any('FAIL' in line for line in out), 'the mutation must be caught: ' + '\n'.join(out))


class NetSessionNoStackSource(unittest.TestCase):
    """net_session.cpp links against the Amiga transport and cannot run on the host: pin the order of the no-stack path in the source."""

    def setUp(self):
        self.src = read('src/rt/net_session.cpp')

    def test_a_join_asks_for_the_stack_before_it_builds_the_node(self):
        for header in ('void netSessionJoinStart(', 'void netSessionJoinRelay('):
            body = function_body(self.src, header)
            self.assertIn('stackPresent(', body, header)
            self.assertLess(body.index('stackPresent('), body.index('initNode('), header + ': initNode (the executable fingerprint) ran before the stack probe')
            # without a stack the function returns before initNode: no node, so netSessionPoll answers NO_STACK
            guard = body[body.index('stackPresent('):body.index('initNode(')]
            self.assertIn('return;', guard, header)

    def test_the_poll_without_a_node_says_no_stack(self):
        body = function_body(self.src, 'void netSessionPoll(')
        self.assertIn('out.eStatus = ms::game::NETST_NO_STACK;', body)
        self.assertRegex(body, r'if\(!pT \|\| !s_s\.isNodeInit\) \{\s*return;')

    def test_begin_refuses_without_a_stack(self):
        body = function_body(self.src, 'bool netSessionBegin(')
        head = body[:body.index('ms::NetNode &n')]
        self.assertRegex(head, r'!pT->hasStack\(\)')
        self.assertIn('return false;', head)

    def test_the_stack_probe_is_one_window_at_most(self):
        body = function_body(self.src, 'bool stackPresent(')
        self.assertIn('wasTried() ? pT->hasStack() : pT->window(', re.sub(r'\s+', ' ', body))


if __name__ == '__main__':
    unittest.main()
