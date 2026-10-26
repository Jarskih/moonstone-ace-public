"""PC network game THROUGH THE RELAY on localhost (docs/NETPLAY.md 2.2b, docs/PC_NET.md): a host and 1..3 joiners, headless (SDL dummy drivers), real time.

    py tests/pc_net/pc_net_relay.py <outdir> [--joiners 1] [--mode coop|classic] [--frames 3000] [--fight]
                                    [--ini h:direct_test=block] [--ini j2:direct=no] ...        extra [net] lines of one instance (h, j1, j2, j3) or of all
                                    [--expect h:D,j1:R]  [--server-args "--no-punch"]  [--port 6611]

What it does: starts a LOCAL lobby / relay server (`py -m tools.netserver --bind 127.0.0.1 --no-connect-back`, never the public one), so the host's game is listed
VIA RELAY; the host registers and keeps an idle relay connection, every joiner opens FIND GAME and joins the listed game through the relay.  All sockets are on
127.0.0.1 (MS_NET_BIND=loopback: no firewall prompt).  Each instance has its own exe copy (the log is written next to it) and its own --pref with a net.ini.
Afterwards it compares the `net: hash tick N crc X` lines of ALL logs (equal at every common tick = lockstep) and reports the `net: udp: direct ...` lines per
instance.  --expect LINK:STATE (D = direct ok on that joiner's link at both its ends, R = `direct failed, via relay` at both, L = direct ok and then lost, N = neither, O = an old client: only the host's `failed`)
turns the report into pass / fail.  The menu timing is a script, not a barrier: a run whose lockstep did not begin is repeated (up to 3 tries).
Exit 0 = lockstep began, every common hash matched, no desync, every --expect met.
"""
import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tests', 'pc_net'))

MASHPATS = ["Space", "Left Space", "Right Space", "Up Space", "Down Space", "Left Up Space", "Right Up Space", "Left Down Space", "Right Down Space", "Left", "Right", "Up", "Down", ""]


def mashs(n, hold=8, seed=3, mul=7):
    s = ''
    for i in range(n):
        keys = MASHPATS[(i * mul + seed) % 14].split()
        s += ''.join(' +' + k for k in keys) + ' @%d' % hold + ''.join(' -' + k for k in keys) + ' @2'
    return s


def free_port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    p = s.getsockname()[1]
    s.close()
    return p


def keys_for(role, mode, idx, joiners, fight):
    """The --keys script of one instance.  Same menu timings as tests/pc_net/pc_net.sh; the joiners find the game in the browser (FIND GAME = the third row)."""
    opening = '@180 +Down @8 -Down @40 +Space @8 -Space @150'
    mode_keys = '+Up @5 -Up @100 +Left @5 -Left @100 +Down @5 -Down @100' if mode == 'classic' else ''
    post = ''
    if fight:
        post = '@100 +Right @40 -Right @10 +Down @20 -Down @50 E @900 +Right_Ctrl @8 -Right_Ctrl @400 +Right_Ctrl @8 -Right_Ctrl @150 2 @300 ' + mashs(150, 8, 10 + idx, 7) + ' @300'
    if role == 'h':
        wait_joiners = ' @%d' % (700 + 800 * (joiners - 1))   # the later joiners take their seats while the host waits
        return (opening + ' +Space @8 -Space @330 ' + mode_keys + ' +Space @20 -Space @70 +Space @20 -Space @70 +Space @20 -Space @500' + wait_joiners +
                ' +Down @12 -Down @70 +Space @20 -Space @300 ' + post)
    find = '+Down @8 -Down @30 +Down @8 -Down @30 +Space @8 -Space @300 +Space @8 -Space @250'
    lobby = '+Space @20 -Space @70 +Space @20 -Space @70 +Left @12 -Left @70 +Space @20 -Space @300'
    return opening + ' ' + find + ' ' + lobby + ' ' + post


def read(path):
    try:
        with open(path, errors='replace') as f:
            return f.read().splitlines()
    except OSError:
        return []


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('out')
    ap.add_argument('--joiners', type=int, default=1)
    ap.add_argument('--mode', default='coop', choices=['coop', 'classic'])
    ap.add_argument('--frames', type=int, default=3000)
    ap.add_argument('--fight', action='store_true', help='after START walk, end the turn, enter the lair and mash the joystick (as pc_net.sh fight)')
    ap.add_argument('--ini', action='append', default=[], help='who:key=value, who in h j1 j2 j3 all')
    ap.add_argument('--expect', default='', help='h:D,j1:R,...  per joiner link')
    ap.add_argument('--server-args', default='')
    ap.add_argument('--server-script', default='', help='run this server.py (with its netproto.py beside it) instead of the repository one: an OLD server for the compatibility run')
    ap.add_argument('--port', type=int, default=0, help='the local lobby server port (default: a free one)')
    ap.add_argument('--game-port', type=int, default=0)
    ap.add_argument('--tries', type=int, default=3)
    ap.add_argument('--stagger', type=float, default=12.0, help='seconds between the starts of two joiners')
    ap.add_argument('--exe', default=os.environ.get('MS_EXE', os.path.join(ROOT, 'build-sdl', 'moonstone.exe')))
    ap.add_argument('--disks', default=os.environ.get('MS_DISKS', ''))
    a = ap.parse_args()
    disks = a.disks
    if not disks:
        disks = os.path.join(ROOT, 'disks')
        if not os.path.isdir(disks):
            common = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--git-common-dir'], capture_output=True, text=True).stdout.strip()
            disks = os.path.join(os.path.dirname(os.path.abspath(os.path.join(ROOT, common))), 'moonstone-ace', 'disks')
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    names = ['h'] + ['j%d' % i for i in range(1, a.joiners + 1)]
    port = a.port or free_port()
    game_port = a.game_port or free_port()
    env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy', MS_NET_BIND='loopback')
    exe_dir = os.path.dirname(a.exe)
    result = 1
    for attempt in range(1, a.tries + 1):
        srv_log = open(os.path.join(out, 'server.log'), 'w')
        srv_cmd = [sys.executable, a.server_script] if a.server_script else [sys.executable, '-m', 'tools.netserver']
        srv = subprocess.Popen(srv_cmd + ['--bind', '127.0.0.1', '--port', str(port), '--no-connect-back'] + a.server_args.split(),
                               cwd=ROOT, stdout=srv_log, stderr=subprocess.STDOUT)
        time.sleep(1.5)
        procs = []
        try:
            for i, who in enumerate(names):
                d = os.path.join(out, who)
                os.makedirs(os.path.join(d, 'pref'), exist_ok=True)
                shutil.copy(a.exe, d)
                shutil.copy(os.path.join(exe_dir, 'SDL3.dll'), d)
                lines = ['[net]', 'public = yes', 'lobby = 127.0.0.1', 'lobby_port = %d' % port]
                lines += [kv.split(':', 1)[1] for kv in a.ini if kv.split(':', 1)[0] in (who, 'all')]
                with open(os.path.join(d, 'pref', 'net.ini'), 'w') as f:
                    f.write('\n'.join(lines) + '\n')
                try:
                    os.remove(os.path.join(d, 'moonstone.log'))
                except OSError:
                    pass
            for i, who in enumerate(names):
                d = os.path.join(out, who)
                if i == 0:
                    t_host = time.time()
                frames = a.frames - (0 if i == 0 else int((time.time() - t_host) * 50) + 100)
                cmd = [os.path.join(d, 'moonstone.exe'), '--disks', disks, '--pref', os.path.join(d, 'pref'), '--skip-intro', '--no-grab', '--frames', str(frames),
                       '--name', ('HOSTY' if i == 0 else 'GUEST%d' % i), '--dump-frame', os.path.join(d, 'end.png'),
                       '--keys', keys_for('h' if i == 0 else 'j', a.mode, i, a.joiners, a.fight)]
                if i == 0:
                    cmd += ['--host', '--port', str(game_port)]
                outf = open(os.path.join(d, 'out.txt'), 'w')
                procs.append((subprocess.Popen(cmd, cwd=d, env=env, stdout=outf, stderr=subprocess.STDOUT), outf))
                if i == 0:   # the joiners start when the host's game is listed via relay and its idle relay connection is up
                    for _ in range(300):
                        if any('relay state 4' in l for l in read(os.path.join(d, 'moonstone.log'))):
                            break
                        time.sleep(0.1)
                else:
                    time.sleep(a.stagger)   # the next joiner follows once this one has its seat
            for p, f in procs:
                p.wait(timeout=1800)
                f.close()
        finally:
            for p, f in procs:
                if p.poll() is None:
                    p.kill()
            srv.terminate()
            try:
                srv.wait(timeout=5)
            except subprocess.TimeoutExpired:
                srv.kill()
            srv_log.close()
        logs = {who: read(os.path.join(out, who, 'moonstone.log')) for who in names}
        if all(any('lockstep begins' in l for l in logs[w]) for w in names):
            break
        print('try %d: the lockstep did not begin on every instance (menu timing), again' % attempt, file=sys.stderr)
    # hashes
    tick_maps = {}
    for who in names:
        m = {}
        for l in logs[who]:
            r = re.search(r'net: hash tick (\d+) crc ([0-9A-F]{8})', l)
            if r:
                m[int(r.group(1))] = r.group(2)
        tick_maps[who] = m
    common = set.intersection(*[set(m) for m in tick_maps.values()]) if tick_maps else set()
    bad = [t for t in sorted(common) if len({m[t] for m in tick_maps.values()}) != 1]
    desync = [l for who in names for l in logs[who] if 'net: desync' in l]
    begun = all(any('lockstep begins' in l for l in logs[w]) for w in names)
    print('hash ticks per instance: %s, common %d (last %s), mismatches %d, desync lines %d' %
          (', '.join('%s %d' % (w, len(tick_maps[w])) for w in names), len(common), max(common) if common else '-', len(bad), len(desync)))
    if bad:
        print('first mismatch at tick %d: %s' % (bad[0], {w: tick_maps[w][bad[0]] for w in names}))
    ok = begun and common and not bad and not desync
    # the direct-path lines
    for who in names:
        print('--', who)
        for l in logs[who]:
            if re.search(r'net: udp: (direct|ok|fallback|session)', l):
                print('   ' + l.strip())
    # expectations: per joiner link, the lines on the host (node k) and on the joiner (its own node)
    if a.expect:
        for item in a.expect.split(','):
            who, want = item.split(':')
            k = int(who[1:])
            host_lines = [l for l in logs['h'] if re.search(r'net: udp: direct (ok|failed).*\(node %d[,)]' % k, l)]
            join_lines = [l for l in logs[who] if re.search(r'net: udp: direct (ok|failed)', l)]
            def kinds(lines):
                return ''.join('D' if 'direct ok' in l else 'R' for l in lines)
            got_h, got_j = kinds(host_lines), kinds(join_lines)
            exp_h, exp_j = {'D': ('D', 'D'), 'R': ('R', 'R'), 'L': ('DR', 'DR'), 'N': ('', ''), 'O': ('R', '')}[want]   # O: the joiner is an old client (direct = no): only the host gives up
            met = got_h == exp_h and got_j == exp_j
            print('expect %s %s: host saw %r, joiner saw %r -> %s' % (who, want, got_h, got_j, 'ok' if met else 'NOT MET'))
            ok = ok and met
    print('RESULT', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
