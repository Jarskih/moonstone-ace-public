#!/usr/bin/env python3
"""Host-side test peer for the moonstone-ace network spike (ROADMAP 12.2, later 12.7/12.12). Standard library only.

  netpeer.py listen [--port 6502]            the Amiga JOINs the PC:  moonstone JOIN 127.0.0.1 (WinUAE's bsdsocket
                                             emulation reaches the PC through the host's own address)
  netpeer.py connect HOST [--port 6502]      the Amiga HOSTs:         moonstone HOST   -> this connects to it

Both echo every 16-byte frame verbatim (frame: 'MSNT', seq u32, tick u32, sender clock u32, big-endian) so the Amiga can
measure the round trip, and print a one-line summary every 50 frames. --delay-ms adds an artificial latency per echo.
"""
import argparse
import socket
import struct
import sys
import time

FRAME = 16


def serve(sock, delay_ms):
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    buf = b''
    n = 0
    last = None
    t0 = time.time()
    tl = t0
    while True:
        data = sock.recv(256)
        if not data:
            print('netpeer: peer closed after %d frames' % n)
            return
        buf += data
        while len(buf) >= FRAME:
            fr, buf = buf[:FRAME], buf[FRAME:]
            magic, seq, tick, clk = struct.unpack('>4sIII', fr)
            if magic != b'MSNT':
                print('netpeer: bad frame %r' % fr)
                continue
            if last is not None and seq != last + 1:
                print('netpeer: seq jump %d -> %d' % (last, seq))
            last = seq
            if delay_ms:
                time.sleep(delay_ms / 1000.0)
            sock.sendall(fr)
            n += 1
            if n % 50 == 0:
                now = time.time()
                print('netpeer: %d frames, seq %d, tick %d, %.1f frames/s over the last 50' % (n, seq, tick, 50 / (now - tl) if now > tl else 0))
                tl = now
                sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('mode', choices=['listen', 'connect'])
    ap.add_argument('host', nargs='?', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=6502)
    ap.add_argument('--delay-ms', type=float, default=0.0)
    ap.add_argument('--retry', type=int, default=60, help='connect: seconds to keep retrying')
    a = ap.parse_args()
    if a.mode == 'listen':
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(('0.0.0.0', a.port))
        srv.listen(1)
        print('netpeer: listening on port %d' % a.port)
        sys.stdout.flush()
        conn, addr = srv.accept()
        print('netpeer: Amiga connected from %s' % (addr,))
        sys.stdout.flush()
        serve(conn, a.delay_ms)
    else:
        end = time.time() + a.retry
        while True:
            try:
                s = socket.create_connection((a.host, a.port), timeout=5)
                break
            except OSError as e:
                if time.time() > end:
                    sys.exit('netpeer: cannot connect to %s:%d: %s' % (a.host, a.port, e))
                time.sleep(0.5)
        s.settimeout(None)
        print('netpeer: connected to the Amiga at %s:%d' % (a.host, a.port))
        sys.stdout.flush()
        serve(s, a.delay_ms)


if __name__ == '__main__':
    main()
