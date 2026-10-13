"""selftest.py -- check a running Moonstone lobby/relay server from the outside (ROADMAP 12.17 / 12.19).

Plays a host and a client against the server, the way the game will:
  1. direct game: the host listens on a TCP port, REGISTERs, the server's connect-back succeeds -> REG_DIRECT; the client
     LISTs, sees the game, JOINs and gets the host's address, connects to it and exchanges a HELLO.
  2. relayed game: a second host REGISTERs a port nobody listens on -> REG_RELAY with a session id; the host opens its idle
     relay connection, the client JOINs (JOIN_RELAY, no address), both meet through the relay and exchange HELLO and INPUT.
  3. version check: a JOIN with another build hash is refused with the host's version.
  4. UDP relay (protocol 6): in a relayed game the server forwards INPUT_UDP (client -> host) and TICK_UDP (host -> client)
     datagrams on the same port number over UDP; the tokens it accepts are the ones it saw in the CONFIG that passed the pipe.

    py tools/netserver/selftest.py                      # server on 127.0.0.1:6510
    py tools/netserver/selftest.py --server lobby.example.org --port 6510

Test 1 needs the server to reach this machine's game port (on a remote server: a forwarded port, or it lists the game as
relay and step 1 reports that).  Step 4 needs UDP on the same port open in the server's firewall.  Standard library only.
"""
import argparse
import os
import socket
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import netproto as P  # noqa: E402

BUILD = 0x12345678
VERSION = (1 << 8) | 0
FRAME = bytes(P.FRAME_BYTES)


def send(sock, msg):
    sock.sendall(P.pack(msg))


def recv(sock, framer, timeout=5.0):
    sock.settimeout(timeout)
    while True:
        st, msg, _ = framer.next()
        if st == P.OK:
            return msg
        if st < 0:
            raise RuntimeError('bad message from the server (status %d)' % st)
        data = sock.recv(4096)
        if not data:
            raise RuntimeError('connection closed')
        framer.push(data)


def connect(host, port):
    s = socket.create_connection((host, port), timeout=5.0)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return s


def hello(name):
    return P.Msg(P.HELLO, build=BUILD, data=0, name=name, pad_mask=1, version=VERSION)


def register(srv, name, game_port):
    s = connect(*srv)
    f = P.Framer()
    send(s, P.Msg(P.REGISTER, **P.listing(name, P.MODE_COOP, 1, 2, BUILD, game_port, VERSION)))
    return s, f, recv(s, f, timeout=10.0)


def list_games(srv):
    s = connect(*srv)
    f = P.Framer()
    send(s, P.Msg(P.LIST_REQ, build=BUILD))
    reply = recv(s, f)
    s.close()
    return reply.entries


def join(srv, game_id, build=BUILD, flags=0):
    s = connect(*srv)
    f = P.Framer()
    send(s, P.Msg(P.JOIN_REQ, game_id=game_id, build=build, version=VERSION, flags=flags))
    reply = recv(s, f)
    s.close()
    return reply


def ip_str(addr):
    return socket.inet_ntoa(struct.pack('>I', addr))


def step(text, ok):
    print('%-58s %s' % (text, 'OK' if ok else 'FAIL'))
    return ok


def test_direct(srv):
    print('-- direct game')
    game = socket.socket()
    game.bind(('0.0.0.0', 0))
    game.listen(1)
    game_port = game.getsockname()[1]
    got = {}

    def accept():
        try:
            game.settimeout(10.0)
            while True:                      # the server's connect-back arrives first and closes at once
                c, _ = game.accept()
                try:
                    msg = recv(c, P.Framer(), timeout=2.0)
                except (RuntimeError, OSError):
                    c.close()
                    continue
                got['hello'] = msg
                send(c, hello('HOST'))
                c.close()
                return
        except OSError:
            pass

    t = threading.Thread(target=accept, daemon=True)
    t.start()
    lob, _, reg = register(srv, 'DIRECT TEST', game_port)
    ok = step('register: result direct (connect-back reached us)', reg.result == P.REG_DIRECT)
    if not ok:
        print('   result %d (1 = relay: the server could not connect back to port %d)' % (reg.result, game_port))
    entries = list_games(srv)
    mine = [gid for gid, l in entries if gid == reg.game_id]
    ok &= step('list: the game is listed (%d games in the list)' % len(entries), bool(mine))
    if mine:
        listed = dict(entries)[reg.game_id]
        ok &= step('list: name, version and no address in the listing',
                   listed['name'] == 'DIRECT TEST' and listed['version'] == VERSION)
    rep = join(srv, reg.game_id)
    ok &= step('join: direct, address %s port %d' % (ip_str(rep.addr), rep.port),
               rep.result == P.JOIN_DIRECT and rep.port == game_port)
    if rep.result == P.JOIN_DIRECT:
        c = connect(ip_str(rep.addr), rep.port)
        send(c, hello('CLIENT'))
        answer = recv(c, P.Framer())
        c.close()
        t.join(5.0)
        ok &= step('game connection: HELLO both ways',
                   got.get('hello') is not None and got['hello'].name == 'CLIENT' and answer.name == 'HOST')
    send(lob, P.Msg(P.UNREGISTER))
    lob.close()
    game.close()
    return ok


def test_relay(srv):
    print('-- relayed game')
    dead = socket.socket()
    dead.bind(('0.0.0.0', 0))
    dead_port = dead.getsockname()[1]
    dead.close()                            # nobody listens there: the connect-back fails
    lob, _, reg = register(srv, 'RELAY TEST', dead_port)
    ok = step('register: result relay, session %d' % reg.session, reg.result == P.REG_RELAY)
    if not ok:
        lob.close()
        return False
    host = connect(*srv)
    hf = P.Framer()
    send(host, P.Msg(P.RELAY_HELLO, session=reg.session, role=P.ROLE_HOST))
    rep = join(srv, reg.game_id)
    ok &= step('join: relay, no host address handed out',
               rep.result == P.JOIN_RELAY and rep.addr == 0 and rep.port == 0)
    cli = connect(*srv)
    cf = P.Framer()
    send(cli, P.Msg(P.RELAY_HELLO, session=rep.session, role=P.ROLE_CLIENT))
    send(cli, hello('CLIENT'))
    m = recv(host, hf)
    ok &= step('relay: client HELLO reaches the host', m.type == P.HELLO and m.name == 'CLIENT')
    send(host, hello('HOST'))
    send(host, P.Msg(P.INPUT, tick=7, node=0, frame=FRAME))
    m1 = recv(cli, cf)
    m2 = recv(cli, cf)
    ok &= step('relay: host HELLO and INPUT reach the client',
               m1.type == P.HELLO and m1.name == 'HOST' and m2.type == P.INPUT and m2.tick == 7)
    for s in (cli, host):
        s.close()
    send(lob, P.Msg(P.UNREGISTER))
    lob.close()
    return ok


def test_udp_relay(srv):
    print('-- UDP relay (protocol 6: per-tick input over UDP with history)')
    dead = socket.socket()
    dead.bind(('0.0.0.0', 0))
    dead_port = dead.getsockname()[1]
    dead.close()
    lob, _, reg = register(srv, 'UDP TEST', dead_port)
    ok = step('register: result relay, session %d' % reg.session, reg.result == P.REG_RELAY)
    if not ok:
        lob.close()
        return False
    host = connect(*srv)
    hf = P.Framer()
    send(host, P.Msg(P.RELAY_HELLO, session=reg.session, role=P.ROLE_HOST))
    rep = join(srv, reg.game_id)
    cli = connect(*srv)
    cf = P.Framer()
    send(cli, P.Msg(P.RELAY_HELLO, session=rep.session, role=P.ROLE_CLIENT))
    send(cli, hello('CLIENT'))
    recv(host, hf)
    token = 0xC0FFEE11
    send(host, P.Msg(P.CONFIG, mode=0, nodes=2, your_node=1, delay=2, seed=1, token=token, bindings=[(0, 0, 0), (1, 0, 1)],
                     names=['HOST', 'CLIENT'], portraits=[2, 0]))
    recv(cli, cf)                                      # the server has now seen the token on its way to the client
    hs = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    cs = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    hs.settimeout(2.0)
    cs.settimeout(2.0)
    inp = P.pack(P.Msg(P.INPUT_UDP, session=reg.session, token=token, node=1, flags=0, ack=0, first=2, count=1, frames=[FRAME]))
    tick = P.pack(P.Msg(P.TICK_UDP, session=reg.session, token=token, node=1, flags=1, ack=3, drop_seq=0, nodes=2, first=2, count=1,
                        frames=[[FRAME, FRAME]]))
    cs.sendto(inp, srv)                                # the client registers
    hs.sendto(tick, srv)                               # the host registers and reaches the client
    try:
        got_tick = cs.recvfrom(2048)[0]
    except OSError:
        got_tick = None
    ok &= step('udp: the host\'s TICK_UDP reaches the client unchanged', got_tick == tick)
    if got_tick is None:
        print('   no datagram came back: is UDP port %d open in the server\'s firewall, and is the server protocol 6?' % srv[1])
    cs.sendto(inp, srv)
    try:
        got_input = hs.recvfrom(2048)[0]
    except OSError:
        got_input = None
    ok &= step('udp: the client\'s INPUT_UDP reaches the host unchanged', got_input == inp)
    bad = P.pack(P.Msg(P.INPUT_UDP, session=reg.session, token=token ^ 1, node=1, flags=0, ack=0, first=2, count=1, frames=[FRAME]))
    cs.sendto(bad, srv)
    hs.settimeout(0.5)
    try:
        hs.recvfrom(2048)
        leaked = True
    except OSError:
        leaked = False
    ok &= step('udp: a datagram with a token nobody was given is dropped', not leaked)
    for sock in (hs, cs, cli, host):
        sock.close()
    send(lob, P.Msg(P.UNREGISTER))
    lob.close()
    return ok


def test_rejoin(srv):
    print('-- a playing game (protocol 5: rejoin)')
    game = socket.socket()
    game.bind(('0.0.0.0', 0))
    game.listen(4)
    game_port = game.getsockname()[1]
    lob, _, reg = register(srv, 'REJOIN TEST', game_port)
    send(lob, P.Msg(P.REFRESH, players=2, state=P.GAME_PLAYING))
    time.sleep(0.3)
    plain = join(srv, reg.game_id)
    ok = step('join without the rejoin flag: refused, the game is playing', plain.result == P.JOIN_FULL)
    rep = join(srv, reg.game_id, flags=P.JOIN_FLAG_REJOIN)
    ok &= step('join with the rejoin flag: the host is named, port %d' % rep.port,
               rep.result in (P.JOIN_DIRECT, P.JOIN_RELAY) and (rep.result == P.JOIN_RELAY or rep.port == game_port))
    listed = [gid for gid, _ in list_games(srv)]
    ok &= step('list: a playing game is not offered', reg.game_id not in listed)
    send(lob, P.Msg(P.UNREGISTER))
    lob.close()
    game.close()
    return ok


def test_version(srv):
    print('-- other build')
    game = socket.socket()
    game.bind(('0.0.0.0', 0))
    game.listen(4)
    lob, _, reg = register(srv, 'VERSION TEST', game.getsockname()[1])
    rep = join(srv, reg.game_id, build=BUILD ^ 1)
    ok = step('join with another build: refused, host version %d.%d' % (rep.host_version >> 8, rep.host_version & 0xFF),
              rep.result == P.JOIN_BUILD and rep.host_version == VERSION)
    send(lob, P.Msg(P.UNREGISTER))
    lob.close()
    game.close()
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--server', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=6510)
    a = ap.parse_args()
    srv = (a.server, a.port)
    results = []
    for test in (test_direct, test_relay, test_udp_relay, test_rejoin, test_version):
        try:
            results.append(test(srv))
        except (OSError, RuntimeError) as e:
            print('   error: %s' % e)
            results.append(False)
    print('selftest: %s' % ('PASS' if all(results) else 'FAIL'))
    return 0 if all(results) else 1


if __name__ == '__main__':
    sys.exit(main())
