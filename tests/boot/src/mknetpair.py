#!/usr/bin/env python3
"""Generates tests/boot/net_pair_host.txt and net_pair_join.txt (the two scripts of tools/netpair.py), net_end_host.txt and net_end_join.txt (`tools/netpair.py --ending`: a game played to the ending) and net_relay_host.txt and net_relay_join.txt (the two
scripts of `tools/netpair.py --relay`) from ONE step list, so that both windows keep exactly the same barrier structure (`sync` + `wait log hash_tick` = the
state-hash line both nodes log every 50 ticks).  The relay pair differs only in how the guest gets into the lobby: the host's game is listed on a local lobby
server that cannot connect back (ROADMAP 12.13a: listed via relay) and the guest picks it in the game browser (FIND GAME, ROADMAP 12.18) instead of typing an
address; from the lobby on the two pairs are the same.

  py tests/boot/src/mknetpair.py            # write all six files
  py tests/boot/src/mknetpair.py --check    # exit 1 when the files on disk differ from the generated text

A step is `B(n, host=[...], join=[...], both=[...])`: n barriers, then the listed lines (`frame` offsets are relative to the last barrier).
Shot names get the window prefix (`{p}` = netpair-h / netpair-j).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.dirname(HERE)

BAR = ['sync', 'wait log hash_tick max 3000']


def hold_key(key, at, frames):
    return ['frame %d key %s down' % (at, key), 'frame %d key %s up' % (at + frames, key)]


def alt_fire(at=5):
    """Fire of the keyboard set B (WASD): left Alt, held 8 frames."""
    return hold_key('LALT', at, 8)


def pokes_strong():
    out = []
    f = 5
    for who in (0, 1):
        for cmd in ('poke who %d' % who, 'poke strength 9', 'poke constitution 30', 'poke endurance 9', 'poke hpmax 310', 'poke hp 310'):
            out.append('frame %d %s' % (f, cmd))
            f += 1
    return out


def pokes_strong_tick(k=10):
    """pokes_strong as tick-pokes: all at hash tick + k, applied in order by every node at the same logical tick (docs/AUTOPLAY.md)."""
    return ['tick hash+%d %s' % (k, cmd) for cmd in ('poke who 0', 'poke strength 9', 'poke constitution 30', 'poke endurance 9', 'poke hpmax 310', 'poke hp 310',
                                                    'poke who 1', 'poke strength 9', 'poke constitution 30', 'poke endurance 9', 'poke hpmax 310', 'poke hp 310')]


class Step:
    def __init__(self, n, host=(), join=(), both=(), raw=False):
        self.n, self.host, self.join, self.both, self.raw = n, list(host), list(join), list(both), raw


def B(n, host=(), join=(), both=()):
    return Step(n, host, join, both)


def RAW(host=(), join=(), both=()):
    """Lines that are not preceded by barriers (waits on log lines / game variables, in order)."""
    return Step(0, host, join, both, raw=True)


PLAN = []
add = PLAN.append

# ---- the lobby is not in lockstep and the game starts on the map: no title menu, no knight screen (NETPLAY.md 4.2b); PRE below picks the knights
# and the first step waits for the knights' log line, then the barriers of the lockstep begin
add(RAW(both=['wait log knight_1_kind max 6000']))
# ---- the map: strong knights, then the leader rotation: the host walks and ends its turn, the join walks with the WASD keys
add(B(6, both=pokes_strong_tick()))   # tick-aligned: a plain poke lands on a different logical tick per window (batch5: the relay's timing made the warp below desync at 1600)
add(B(2, both=['frame 5 shot {p}-map', 'frame 6 poke dump 1']))
add(B(1, host=['frame 5 joy1 right', 'frame 45 joy1 none', 'frame 60 key E down', 'frame 75 key E up'],
      join=['frame 5 key D down', 'frame 45 key D up']))        # only the leader (host) moves; the join's D must be ignored
add(RAW(both=['frame 5 poke dump 1', 'wait var turn 1 max 6000', 'wait var turn 0 max 20000']))
add(B(2, host=['frame 5 joy1 fire pulse 12'], join=hold_key('LALT', 5, 12)))   # "Next Day": run 1 pressed too early (the screen was not polling yet: MISSED)
add(B(4, both=['frame 5 shot {p}-leader2', 'frame 6 poke dump 1']))
add(B(1, host=['frame 5 joy1 right', 'frame 45 joy1 none'],
      join=['frame 5 key D down', 'frame 45 key D up']))        # now the join (Keys WASD) is the leader: it walks, the host's stick is ignored
add(B(2, both=['frame 5 shot {p}-walk2', 'frame 6 poke dump 1']))
add(B(1, both=['tick hash+10 poke who 1', 'tick hash+10 poke warp_lair 0', 'tick hash+10 poke lair_left 5']))   # one tick on both nodes (the proximity scan sees the warp at the same tick)
add(B(2, join=alt_fire()))                                       # the lair: the leader's (join) fire
add(B(12, both=['frame 5 shot {p}-fight', 'frame 6 poke fightdump 1']))
# both pads play the fight (the join with `mash wasd`, the keyboard set B).  The batch run that froze the join at tick 3899 / desynced at 3750 was
# not reproduced on a quiet machine (hashes equal to tick 6700); the fight needs ~3000 ticks (18 creatures), hence the long scene wait
add(B(1, host=['frame 5 mash joy1 10'], join=['frame 5 mash wasd 10']))
add(RAW(both=['wait var scene 2 max 40000']))
add(B(0, host=['frame 1 mash joy1 off', 'frame 2 joy1 none'], join=['frame 1 mash wasd off']))
add(B(1, both=['frame 5 shot {p}-loot', 'frame 6 poke dump 1']))
add(B(1, both=['tick hash+10 poke cursorx 308', 'tick hash+10 poke cursory 140']))
add(B(1, host=['frame 5 joy1 fire pulse 20'], join=hold_key('LALT', 5, 20)))   # leave the loot screen (the leader's fire counts); 20 frames: the loot loop polls only every few ticks (batch5 --tcp: a short press can fall between two polls, the drop then hit the loot screen)
add(B(2, both=['frame 5 shot {p}-end', 'frame 6 poke dump 1']))
# ---- the drop check, event-driven (the two windows run on unrelated clocks, so no step may depend on how long the other one is silent):
# the join window goes silent (`poke netsilent 1`: it sends no input and stays silent until the host drops it; one shot shows its frozen picture),
# the host's stalled tick brings up the WAITING FOR box (log `box waiting`), the script presses fire (30 frames: a press is an edge and the
# window reads the stick only between redraws), waits for CONTINUE WITHOUT (log `box confirm`) and presses again (>= 60 frames later, within 4 s):
# `the player stopped waiting`.  The host drops the guest (BYE dropped), which ends the join window's silence (log `session ended`), and plays on alone.
add(B(1, join=['frame 5 poke netsilent 1', 'frame 40 shot netpair-j-silent']))
add(RAW(host=['sync', 'wait log box_waiting max 3000'], join=['sync', 'wait log session_ended max 6000']))
add(RAW(host=['frame 60 shot netpair-h-waiting', 'frame 130 joy1 fire pulse 30', 'wait log box_confirm max 1500']))
add(RAW(host=['frame 70 joy1 fire pulse 30', 'wait log the_player_stopped_waiting max 1500']))
# ---- the rejoin step (ROADMAP M12, NETPLAY.md 4.5), event-driven like the drop check.  The host plays on alone; the dropped window leaves its
# lone game with Q on the map (the farewell screen, fire, the title menu), opens the Multiplayer screen, where the REJOIN row has appeared (the
# window remembers the game: `net: rejoin memory`), and presses it (`poke netaction 5`).  The host shows its note ("<NAME> REJOINS AT THE NEXT TURN",
# log `will enter at the next map turn`), ends its map turn (E, Next Day) and the rejoin happens at that turn: the host logs `map turn at tick`,
# both sides `is back at tick`, the rejoiner `lockstep running as node` - and then the hash barriers of both windows are equal again.
REJOIN_BARS = BAR + BAR
# The tail of both windows is the SAME: two bars, the shot (an 8 s freeze of this window: the peer's lockstep stands still meanwhile, its WAITING FOR box comes up -
# that is the stall of batch3/4 at tick +62..+84, not a hang), the dump, one more bar, quit 100 frames later.  Both windows reach every bar within a few ticks of each
# other, so whichever quits first leaves the other with its last bar already consumed; the earlier tail (the rejoined window waited two bars MORE than the host)
# asked a host that quits 45 ticks after its second bar for two bars it never plays: a wait-timeout on hash_tick in the join window, NETPAIR FAIL.
LAST_HOST = ['frame 60 shot netpair-h-dropped', 'frame 70 poke dump 1', 'frame 200 joy1 right', 'frame 240 joy1 none', 'frame 300 shot netpair-h-alone',
             'frame 310 poke dump 1',
             'wait log will_enter_at_the_next_map max 40000',     # (wait texts are at most 27 characters)
             'frame 20 shot netpair-h-note',
             'frame 200 key E down', 'frame 215 key E up',
             'wait log map_turn_at_tick max 8000',                # the rejoin comes at the next map turn (the guest's): no waiting for the turn to come back
             'wait log is_back_at_tick max 4000'] + REJOIN_BARS + [
             'frame 5 shot netpair-h-rejoined', 'frame 6 poke dump 1'] + BAR + ['frame 100 quit']
LAST_JOIN = ['frame 30 shot netpair-j-after',
             'frame 100 key Q tap',                               # the map: Q = the farewell screen (game over text), fire, then the title menu
             'frame 250 joy1 fire pulse 20',
             'wait input 10 max 3000',
             'frame 60 poke menuact 1',
             'wait log screen_open max 1500',
             'frame 30 shot netpair-j-rejoin-multi',              # the REJOIN row is there
             'frame 1 poke netaction 5',
             'frame 150 shot netpair-j-rejoin-wait',
             'wait log lockstep_running_as_node max 40000'] + REJOIN_BARS + [
             'frame 5 shot netpair-j-rejoined', 'frame 6 poke dump 1'] + BAR + ['frame 100 quit']

# ---- the second output set, net_end_host.txt / net_end_join.txt: a normal two-player game played to the ending (tools/netpair.py --ending).
# Same lobby flow and same barrier structure as above, but no fight and no drop: the map for two rounds (the host leads and walks, ends its turn, Next Day,
# the join leads and walks), then the state of tests/boot/src/play_end.mk (all four moonstones, the party on Stonehenge, node 7) set by TICK-POKES: a plain
# `poke` fires from the VBL interrupt at whatever tick each window is on, a tick-poke is applied by the lockstep tick itself, at the same tick `H + K` on both
# windows (H = the tick of the hash line the barrier released; docs/AUTOPLAY.md "Tick-aligned pokes"), so the states stay equal (the next hash proves it).
# Then the leader's fire (the join) enters Stonehenge and the ending starts on both windows at the same tick.  The ending is the program overlay, outside the
# lockstep: no more hash barriers; both windows play it on their own timers (the shots follow tests/boot/play_end.txt) and the session closes by itself
# (NETPLAY.md 4.6): the host's listing goes at "quest is complete", the connection after the ending, then the main menu.
END = []
add_end = END.append
add_end(RAW(both=['wait log knight_1_kind max 6000']))
add_end(B(6, both=['frame 5 poke dump 1']))                       # the map settles (the net_pair run pokes its strong knights here)
add_end(B(2, both=['frame 5 shot {p}-map', 'frame 6 poke dump 1']))
add_end(B(1, host=['frame 5 joy1 right', 'frame 45 joy1 none', 'frame 60 key E down', 'frame 75 key E up'],
          join=['frame 5 key D down', 'frame 45 key D up']))      # only the leader (host) moves; the join's D must be ignored
add_end(RAW(both=['frame 5 poke dump 1', 'wait var turn 1 max 6000', 'wait var turn 0 max 20000']))
add_end(B(2, host=['frame 5 joy1 fire pulse 12'], join=hold_key('LALT', 5, 12)))   # "Next Day"
add_end(B(4, both=['frame 5 shot {p}-leader2', 'frame 6 poke dump 1']))
add_end(B(1, host=['frame 5 joy1 right', 'frame 45 joy1 none'],
          join=['frame 5 key D down', 'frame 45 key D up']))      # round 2: the join is the leader and walks
add_end(B(2, both=['frame 5 shot {p}-walk2', 'frame 6 poke dump 1']))
# What play_end's `@SET 1630` does: `poke moonstones 15` (every phase: Stonehenge ends the game) and `poke warp_node 7` (Stonehenge) on the knight `poke who N`
# selected.  In co-op the stone owner is any party member holding the stone of the phase and the leader's record carries the party token, so both members get
# the stones and the leader (knight 1 in round 2) is warped.  The pokes sit at ticks H+10 / H+12, safely ahead of both windows (they are reached a few ticks
# behind the barrier tick H at most) and before the next hash bar (H+50), which therefore covers the poked state.
add_end(B(1, both=['tick hash+10 poke who 0', 'tick hash+10 poke moonstones 15', 'tick hash+10 poke who 1', 'tick hash+10 poke moonstones 15',
                   'tick hash+12 poke warp_node 7']))
add_end(B(1, both=['frame 5 shot {p}-stonehenge', 'frame 6 poke dump 1']))   # the hash of this bar contains the poked state: equal on both windows or the run fails
# Two fires, as in play_end.mk (pulse, shot quest, pulse): the leader's fire on the map enters Stonehenge (the join's Alt, its WASD set is the leader's
# controller; the host's stick is not: the map reads the current member's pad only), the place loads files (a file open releases every held input) and shows
# "You have completed the quest" (the mog place text); that text waits for a fresh fire of ANY controller (rtMogWaitFireC: the "press fire" waits of a
# co-op game take every pad, so both presses below count), and only then the program overlay plays the ending.  Runs 2 and 3 had no second press before
# `wait log enter_program` (the host's stray pulse 500 frames later was the only thing that ever ended the wait, and in run 3 it did not).
add_end(B(1, join=hold_key('LALT', 5, 20)))                       # fire 1, the leader's: the map enters Stonehenge
add_end(B(2, both=['frame 5 shot {p}-quest', 'frame 6 poke dump 1']))   # the quest text is up (the load stops the ticks, then 20 ticks of wait)
add_end(B(1, host=['frame 5 joy1 fire pulse 20'], join=hold_key('LALT', 5, 20)))   # fire 2: any controller ends the text, the ending starts
# From here on no hash barrier (the ending is classic code outside the lockstep).  `sync` in the last BAR forgot the boot's "enter program" hit.
END_TAIL = [
    'wait log enter_program max 3000',                           # rtGameRun: enter program = the overlay switch to the ending (caused by fire 2 above)
    'frame 500 shot {p}-ceremony', 'frame 500 shot {p}-orb', 'frame 1000 shot {p}-dancers', 'frame 1500 shot {p}-vortex', 'frame 1500 shot {p}-epilogue',
    'wait log enter_mog max 8000',                               # the ending is over, the session was closed, the main menu comes up
    'frame 300 shot {p}-title',
    'frame 400 quit']

HEAD_END = """# ROADMAP 12.19 end-to-end run, {side} window: a normal two-player network game played to the ending (run with `py tools/netpair.py --ending`, never alone; generated
# by tests/boot/src/mknetpair.py, edit that).  {side_doc}
# Lobby and barriers as in net_pair_*.txt (state-hash barriers every 50 ticks).  Flow: lobby -> START -> map, round 1 the host walks and ends its turn, Next Day,
# round 2 the join walks -> tick-pokes (all moonstones, the party on Stonehenge: the same tick on both windows, docs/AUTOPLAY.md) -> one more hash bar proves the
# states equal -> the leader's fire enters Stonehenge, a second fire (any pad) ends its quest text -> the ending on both windows (outside the lockstep) -> the session closes -> the main menu -> quit.
"""

SIDE_DOC_END = {
    'host': "Companion: net_end_join.txt.  The host leads round 1.",
    'join': "Companion: net_end_host.txt.  The join leads round 2 and makes the final move (its fire enters Stonehenge).",
}

HEAD_COMMON = """# ROADMAP 12.8b/12.11/3.2 end-to-end run, {side} window (run with tools/netpair.py{relay_opt}, never alone; generated by tests/boot/src/mknetpair.py, edit that).
# {side_doc}
# Deterministic by barriers, not by frame timing: in lockstep every step waits for the next state-hash log line (`sync` + `wait log hash_tick`,
# logged by both nodes every 50 ticks) and acts 5 frames behind it, because a window that waits for its peer's tick does not read the stick and a
# pulse then would be MISSED.  Shots sit on the same barrier in both scripts so both windows freeze together.  Pokes change game state locally, so both
# scripts apply the same pokes behind the same barrier.
# Flow (NETPLAY.md 4.2b): main menu -> MULTIPLAYER (`poke menuact 1`, no cursor counting) -> HOST GAME (PUBLIC YES) / CONNECT -> the lobby = the knight screen
# (NOT in lockstep, so the scripts wait on log lines, not on barriers): the host takes portrait 0 and keeps Joystick 1, the join window starts on portrait 1
# (a joiner starts on the lowest knight nobody stands on), takes it and picks Keys WASD -> START (host) -> the map at once (no title menu, no knight screen): the host walks with the stick, the join window (leader of
# round 2) with W A S D / Alt -> a lair fight (both pads mash, see the PLAN comment) -> loot -> the drop
# check (the guest goes silent, the host's WAITING FOR GUEST box, fire twice = CONTINUE WITHOUT, the host plays on alone) -> the rejoin (the guest
# goes back to the title menu, REJOIN GAME, enters at the host's next map turn, equal hashes again) -> quit.
"""

HOST_PRE = """frame 300 key SPACE tap
wait input 10 max 3000
frame 60 poke menuact 1
wait log screen_open max 1500
frame 30 shot netpair-h-multi
# HOST GAME by poke (PUBLIC YES is the default): straight into the lobby (the knight screen); the bottom line is START, grey until the guest picked
frame 1 poke netaction 1
wait log lobby_open max 3000
frame 400 shot netpair-h-listed
wait log peers_1 max 15000
# `frame N` counts from the barrier above and the parser sorts a segment by N: the steps below are spaced out, not "N after the previous line".
# The lobby loop redraws after every change (a draw and the 6-frame repeat wait take ~20 frames) and reads the stick only between redraws:
# every step sits >= 60 frames after the previous one and a press lasts 30 frames (a direction 12: it must not be read twice).
frame 60 shot netpair-h-lobby
# portrait 0 (the selector starts on it), fire; the name HOST is kept (fire), the Control line keeps Joystick 1 (fire)
frame 70 joy1 fire pulse 30
frame 150 shot netpair-h-name
frame 160 joy1 fire pulse 30
frame 240 shot netpair-h-control
frame 250 joy1 fire pulse 30
wait log lobby_seat_0_done max 3000
wait log lobby_seat_1_done max 15000
frame 60 shot netpair-h-picked
# START: the barrier above fires while the host redraws for the guest's last pick, so the stick waits a while; down from the portraits, fire.
# `poke netaction 3` (START through the same code path as fire on it) is the test-only fallback should the stick still not land: it does nothing
# once the lobby is left.  It sits right behind the fire, not 200 frames later: the lockstep runs a tick per frame from the moment of START, and
# the host's first `sync` below must come before the second state-hash line (tick 100), else the host's barriers run one hash ahead of the join
# window's for the rest of the script (pokes and shots land at different ticks: the "desync" at the first hash behind them, run 94/95).
frame 100 joy1 down pulse 20
frame 190 joy1 fire pulse 30
frame 215 poke netaction 3
wait log lockstep_begins max 12000"""

JOIN_HEAD = """frame 300 key SPACE tap
wait input 10 max 3000
frame 60 poke menuact 1
wait log screen_open max 1500
frame 30 shot netpair-j-multi
"""

JOIN_CONNECT = """# CONNECT by poke (netpair.py starts this window later, the host listens by now); a refused connect shows "no answer" and the next poke dials again.
# The host seats us and the screen goes to the lobby.
frame 1 poke netaction 2
frame 150 shot netpair-j-wait
frame 600 poke netaction 2
wait log lobby_open max 12000
"""

# the game browser (ROADMAP 12.18): `poke netaction 4` is FIND GAME through the code path of fire on its row; the browser asks the lobby named in net.ini, the list
# holds the host's game (listed via relay on the local lobby server, netpair.py --relay starts this window only then), fire on it joins: JOIN_REQ -> JOIN_REPLY
# relay -> a connection to the lobby server with RELAY_HELLO -> the host seats us and the screen goes to the lobby
JOIN_FIND = """# FIND GAME by poke (the same code path as fire on its row; netpair.py --relay starts this window when the host's game is listed via relay on the local
# lobby server): the game browser asks the lobby (net.ini lobby / lobby_port), the list has the host's game, fire on it: JOIN_REQ -> JOIN_REPLY relay -> a
# connection to the lobby server with RELAY_HELLO -> the host seats us and the screen goes to the lobby.
frame 1 poke netaction 4
wait log find_screen_open max 1500
wait log find_status_2_games_1 max 3000
frame 60 shot netpair-j-find
frame 70 joy1 fire pulse 30
wait log lobby_open max 12000
"""

JOIN_TAIL = """frame 60 shot netpair-j-lobby
# the join window starts on portrait 1 (the host's is 0): fire, the name GUEST is kept (fire), the Control line: Joystick 1 -> Keys WASD (left wraps), fire
wait log lobby_seat_0_done max 15000
frame 30 joy1 fire pulse 30
frame 110 shot netpair-j-name
frame 120 joy1 fire pulse 30
frame 200 joy1 left pulse 12
frame 280 shot netpair-j-control
frame 290 joy1 fire pulse 30
wait log lockstep_begins max 12000"""

JOIN_PRE = JOIN_HEAD + JOIN_CONNECT + JOIN_TAIL
JOIN_PRE_RELAY = JOIN_HEAD + JOIN_FIND + JOIN_TAIL

# the host: HOST_PRE as it is (the listing is made by the lobby screen); the relay run only adds a barrier for the log line that says the idle relay connection is up
HOST_PRE_RELAY = HOST_PRE.replace('wait log lobby_open max 3000\n', 'wait log lobby_open max 3000\nwait log relay_state_4 max 6000\n', 1)

SIDE_DOC = {
    'host': "Companion: net_pair_join.txt.  The host's stick drives its own side of the lobby and keeps Joystick 1; the game starts with its START button.",
    'join': 'Companion: net_pair_host.txt.  The game is started with JOIN 127.0.0.1, so the address and port are pre-filled and nothing is typed.',
}
SIDE_DOC_RELAY = {
    'host': "Companion: net_relay_join.txt.  The same host as net_pair_host.txt, but its game is listed on a local lobby server that cannot connect back, i.e. via relay: "
            "the log line `net: relay state 4` says the idle relay connection is up.",
    'join': 'Companion: net_relay_host.txt.  No address is typed or pre-filled: the guest opens FIND GAME and joins the listed game through the relay.',
}


def render(side, relay=False, end=False):
    """The script text of one window.  end=True: the net_end_* pair (the lobby texts are the direct run's, their shot names get the netend- prefix)."""
    doc = (SIDE_DOC_RELAY if relay else SIDE_DOC)[side]
    if end:
        lines = [HEAD_END.format(side=side.upper(), side_doc=SIDE_DOC_END[side]).rstrip('\n')]
    else:
        lines = [HEAD_COMMON.format(side=side.upper(), side_doc=doc, relay_opt=' --relay' if relay else '').rstrip('\n')]
    if relay:
        lines.append(HOST_PRE_RELAY if side == 'host' else JOIN_PRE_RELAY)
    else:
        lines.append(HOST_PRE if side == 'host' else JOIN_PRE)
    prefix = 'netpair-h' if side == 'host' else 'netpair-j'
    is_first = True
    for st in (END if end else PLAN):
        if not st.raw:
            for _ in range(st.n):
                # The very first barrier takes the hash line of tick 50 whenever the window gets there: no `sync` (it would drop a line already
                # logged), so a window that is up to one hash late still counts the same hash lines as its peer.
                lines.extend(BAR[1:] if is_first else BAR)
                is_first = False
        acts = list(st.both) + list(st.host if side == 'host' else st.join)
        if st.raw or st.n == 0:
            acts = sorted(acts, key=lambda a: a.startswith('wait'))   # frame lines first, the waits last (a wait ends the segment)
        else:
            acts = sorted(acts, key=lambda a: (a.startswith('wait'), int(a.split()[1]) if a.startswith('frame') else 0))
        lines.extend(a.replace('{p}', prefix) for a in acts)
    if end:
        lines.extend(a.replace('{p}', prefix) for a in END_TAIL)
        return '\n'.join(lines).replace('netpair-', 'netend-') + '\n'
    lines.extend(LAST_HOST if side == 'host' else LAST_JOIN)
    return '\n'.join(lines) + '\n'


# ---------------------------------------------------------------------------------------------------------------------------------------
# The 4-player co-op run (net_quad_*.txt, `py tools/netpair.py --quad`) and the classic run (net_classic_*.txt, `py tools/netpair.py --classic`).
# Same barrier structure as above (`sync` + `wait log hash_tick`), but a step carries a function window index -> lines, because four windows
# do four different things.  Shot prefixes: netpair-q<k> (quad, window k), netpair-ch / netpair-cj (classic host / join).

def STEP(n, acts, raw=False):
    """n barriers, then acts(k) = the lines of window k (k = 0 host, 1.. the joiners); raw: waits on game variables, no barriers before them."""
    return (n, acts, raw)


def render_steps(head, pre, plan, k, prefix, last):
    lines = [head.rstrip('\n'), pre.rstrip('\n')]
    is_first = True
    for n, acts_fn, raw in plan:
        for _ in range(n):
            lines.extend(BAR[1:] if is_first else BAR)   # the first barrier takes whatever hash line comes (no `sync`), as render() does
            is_first = False
        acts = list(acts_fn(k))
        if raw or n == 0:
            acts = sorted(acts, key=lambda a: a.startswith('wait'))
        else:
            acts = sorted(acts, key=lambda a: (a.startswith('wait'), int(a.split()[1]) if a.startswith('frame') else 0))
        lines.extend(a.replace('{p}', prefix) for a in acts)
    lines.extend(last)
    return '\n'.join(lines) + '\n'


QUAD_PLAYERS = 4


def quad_plan():
    plan = [STEP(0, lambda k: ['wait log knight_3_kind max 6000'], raw=True),   # the last of the four knights' log lines (netpair-style: right behind the START)
            STEP(6, lambda k: ['frame 5 poke dump 1']),
            STEP(2, lambda k: ['frame 5 shot {p}-map', 'frame 6 poke dump 1'])]
    for r in range(QUAD_PLAYERS):
        nxt = (r + 1) % QUAD_PLAYERS
        # round r: knight r leads.  EVERY window pushes its own stick right (and the dump after it says whose knight moved), only the leader's machine
        # moves the token and its E (its keyboard) ends the turn: a party of four leaves no black knight on the map, the round ends at once ("Next Day")
        plan.append(STEP(1, lambda k, r=r: ['frame 5 joy1 right', 'frame 45 joy1 none', 'frame 50 poke dump 1'] + (['frame 60 key E down', 'frame 75 key E up'] if k == r else [])))
        plan.append(STEP(0, lambda k, nxt=nxt: ['frame 5 poke dump 1', 'wait var leader %d max 6000' % nxt], raw=True))
        # "Next Day" waits for a fire of ANY controller: the host and the new leader press (two barriers behind, the screen polls by then)
        plan.append(STEP(2, lambda k, nxt=nxt: ['frame 5 joy1 fire pulse 12'] if k in (0, nxt) else []))
        plan.append(STEP(4, lambda k, nxt=nxt: ['frame 5 shot {p}-lead%d' % nxt, 'frame 6 poke dump 1']))
    plan.append(STEP(2, lambda k: ['frame 5 shot {p}-end', 'frame 6 poke dump 1']))
    return plan


QUAD_LAST = ['frame 100 quit']

QUAD_HEAD = """# ROADMAP M12 4-player co-op over the network, window {k} ({who}) of tools/netpair.py --quad (never run alone; generated by tests/boot/src/mknetpair.py, edit that).
# {side_doc}
# Lobby (NETPLAY.md 4.2b, not in lockstep): the host takes portrait 0, the joiners 1, 2 and 3 (a joiner starts on the lowest knight nobody stands on, and
# tools/netpair.py starts the next window only when the previous one is seated, so the seats come in window order); every machine keeps its name and its own
# Joystick 1.  Each joiner waits for the previous seat's `lobby_seat_N_done`, the host for all four, then START.  The map (lockstep, barriers as in
# net_pair_*.txt): four rounds, the leader rotates 0, 1, 2, 3 (`wait var leader`); in each round every window pushes its stick right and only the leader's
# machine moves the token and ends the turn with E; "Next Day" is pressed by the host and the new leader.
"""

QUAD_SIDE_DOC = {0: "Companion: net_quad_join1..3.txt.  The host: portrait 0, START when all four are done.",
                 1: "Companion: net_quad_host.txt.  Joins first (portrait 1), leads round 2.",
                 2: "Companion: net_quad_host.txt.  Joins second (portrait 2), leads round 3.",
                 3: "Companion: net_quad_host.txt.  Joins third (portrait 3), leads round 4."}


def quad_pre(k):
    prefix = 'netpair-q%d' % k
    if k == 0:
        pre = HOST_PRE.replace('wait log lobby_seat_1_done max 15000\n', 'wait log lobby_seat_1_done max 15000\nwait log lobby_seat_2_done max 30000\nwait log lobby_seat_3_done max 30000\n', 1)
        assert 'lobby_seat_3_done' in pre
        return pre.replace('netpair-h-', prefix + '-')
    pre = JOIN_HEAD + JOIN_CONNECT + """frame 60 shot netpair-j-lobby
# portrait %d (where this selector starts): fire, the name is kept (fire), the Control line keeps Joystick 1 (fire)
wait log lobby_seat_%d_done max 30000
frame 30 joy1 fire pulse 30
frame 110 shot netpair-j-name
frame 120 joy1 fire pulse 30
frame 200 shot netpair-j-control
frame 210 joy1 fire pulse 30
wait log lockstep_begins max 12000""" % (k, k - 1)
    return pre.replace('netpair-j-', prefix + '-')


def render_quad(k):
    head = QUAD_HEAD.format(k=k, who='host' if k == 0 else 'join %d' % k, side_doc=QUAD_SIDE_DOC[k])
    return render_steps(head, quad_pre(k), quad_plan(), k, 'netpair-q%d' % k, QUAD_LAST)


# ---- the classic pair: the host switches MODE to CLASSIC on the lobby's MODE row (up from the portraits, fire), both pick a knight (no controller line in classic:
# every knight plays on Joystick 1 of its own machine), START.  Classic map: each human knight has its own turn, played with its own machine's stick and keyboard;
# the other machine's stick is ignored; the black knights move after the second human, then "Next Day".  Round 2: the host walks again, the guest's knight is
# put into a lair (the pair script's pokes) and fights it on its own machine (the fight reads the fighter's own stick: game/net_state), the loot is left, quit.
def classic_plan():
    walk = ['frame 5 joy1 right', 'frame 45 joy1 none', 'frame 50 poke dump 1']
    ekey = ['frame 60 key E down', 'frame 75 key E up']
    return [
        STEP(0, lambda k: ['wait log knight_1_kind max 6000'], raw=True),
        STEP(6, lambda k: pokes_strong_tick()),                                   # state pokes are tick-aligned: a plain poke lands on a different tick per window
        STEP(2, lambda k: ['frame 5 shot {p}-map', 'frame 6 poke dump 1']),
        STEP(1, lambda k: walk + (ekey if k == 0 else [])),                       # turn of knight 0: the host's machine only
        STEP(0, lambda k: ['frame 5 poke dump 1', 'wait var turn 1 max 6000'], raw=True),
        STEP(2, lambda k: ['frame 5 shot {p}-turn2', 'frame 6 poke dump 1']),
        STEP(1, lambda k: walk + (ekey if k == 1 else [])),                       # turn of knight 1: the guest's machine only
        STEP(0, lambda k: ['frame 5 poke dump 1', 'wait var turn 0 max 30000'], raw=True),   # the black knights move, the day ends
        STEP(0, lambda k: ['wait log next_day_waits max 6000'], raw=True),        # "Next Day" polls for fire only after the black knights have moved (batch5: a pulse 2 bars after
        STEP(0, lambda k: ['frame 5 joy1 fire pulse 12'], raw=True),              # `turn 0` came too early, the screen stayed up, the next `wait var turn` timed out, the windows drifted a bar)
        STEP(4, lambda k: ['frame 5 shot {p}-day2', 'frame 6 poke dump 1']),
        STEP(1, lambda k: walk + (ekey if k == 0 else [])),                       # round 2, the host again
        STEP(0, lambda k: ['frame 5 poke dump 1', 'wait var turn 1 max 6000'], raw=True),
        STEP(1, lambda k: ['tick hash+10 poke who 1', 'tick hash+10 poke warp_lair 0', 'tick hash+10 poke lair_left 5']),   # tick-pokes, in order, one tick on both nodes
        STEP(2, lambda k: ['frame 5 joy1 fire pulse 12'] if k == 1 else []),      # the lair: the guest's fire
        STEP(12, lambda k: ['frame 5 shot {p}-fight', 'frame 6 poke fightdump 1']),
        STEP(1, lambda k: ['frame 5 mash joy1 10'] if k == 1 else []),      # only the fighter's (guest's) window plays; the host's stick would be ignored by the merge anyway
        STEP(0, lambda k: ['wait var scene 2 max 40000'], raw=True),
        STEP(0, lambda k: ['frame 1 mash joy1 off', 'frame 2 joy1 none'], raw=True),
        STEP(0, lambda k: ['wait log screen_ready_2 max 6000'], raw=True),         # scene 2 is stored before fade-out; the loot shot needs the completed redraw and palette
        STEP(1, lambda k: ['frame 5 shot {p}-loot', 'frame 6 poke dump 1']),
        STEP(1, lambda k: ['tick hash+10 poke cursorx 308', 'tick hash+10 poke cursory 140']),
        STEP(1, lambda k: ['frame 5 joy1 fire pulse 20']),                        # leave the loot screen (the guest's fire counts; 20 frames, see the pair plan)
        STEP(2, lambda k: ['frame 5 shot {p}-end', 'frame 6 poke dump 1']),
        STEP(1, lambda k: ['tick hash+10 poke who 1', 'tick hash+10 poke warp_node 4']), # Town A, from the guest knight's next map turn
        STEP(1, lambda k: ['frame 5 joy1 fire pulse 8'] if k == 1 else []),       # enter the town on the guest's own machine
        STEP(0, lambda k: ['wait file HighWood.piv max 6000', 'wait input 10 gap 6 max 6000'], raw=True),
        STEP(1, lambda k: ['frame 5 shot {p}-town', 'frame 6 poke dump 1']),
        STEP(1, lambda k: ['tick hash+10 poke cursorx 290', 'tick hash+10 poke cursory 183']), # align both town cursors with the EXIT button
        STEP(1, lambda k: ['frame 5 joy1 fire pulse 12'] if k == 1 else []),      # only the guest's turn-owner pad selects EXIT
        STEP(0, lambda k: ['wait log next_day_waits max 6000'], raw=True),     # exiting town spends the turn; black knights move before this wait
        STEP(1, lambda k: ['frame 5 shot {p}-town-nextday', 'frame 6 poke dump 1']),
        STEP(1, lambda k: ['frame 5 joy1 fire pulse 12'] if k == 1 else []), # the guest can advance Next Day from its own pad
        STEP(2, lambda k: ['frame 5 shot {p}-town-map', 'frame 6 poke dump 1']),
    ]


CLASSIC_LAST = ['frame 100 quit']

CLASSIC_HEAD = """# ROADMAP M12 classic network game, {side} window of tools/netpair.py --classic (never run alone; generated by tests/boot/src/mknetpair.py, edit that).
# {side_doc}
# Lobby: the host moves up from the portraits to the MODE row and presses fire (COOP -> CLASSIC, log `net: lobby mode classic`), then both pick a knight
# (host portrait 0, guest portrait 1; classic has no controller line: each knight plays on Joystick 1 of its own machine).  Map (barriers as in net_pair_*.txt):
# round 1 knight 0 (host) walks and ends its turn, knight 1 (guest) walks and ends its turn, the black knights move, "Next Day"; round 2 the host walks, the guest's
# knight is put into a lair and fights it with its own stick (the host's stick is ignored), loot, quit.  In every turn BOTH windows push their stick right: only
# the turn knight's machine may move it (the dumps show it).
"""

CLASSIC_SIDE_DOC = {0: "Companion: net_classic_join.txt.  The host switches the mode and plays knight 0.",
                    1: "Companion: net_classic_host.txt.  Plays knight 1 (portrait 1)."}


def classic_pre(k):
    prefix = 'netpair-ch' if k == 0 else 'netpair-cj'
    if k == 0:
        i = HOST_PRE.index('frame 60 shot netpair-h-lobby')
        j = HOST_PRE.index('wait log lobby_seat_1_done max 15000')
        mid = """frame 60 shot netpair-h-lobby
# the MODE row is up from the portraits; fire switches it (COOP -> CLASSIC), the log line says the room has it; then down to the portraits again
frame 70 joy1 up pulse 12
frame 150 shot netpair-h-mode-coop
frame 160 joy1 fire pulse 30
wait log lobby_mode_classic max 3000
frame 60 shot netpair-h-mode
# One row only: allow the lobby's 6-frame poll to see it, without repeating onto START.
frame 70 joy1 down pulse 6
# portrait 0 (the selector starts on it), fire; the name HOST is kept (fire); classic has no Control line: the pick is done
frame 150 joy1 fire pulse 30
frame 230 shot netpair-h-name
frame 240 joy1 fire pulse 30
wait log lobby_seat_0_done max 3000
"""
        return (HOST_PRE[:i] + mid + HOST_PRE[j:]).replace('netpair-h-', prefix + '-')
    pre = JOIN_HEAD + JOIN_CONNECT + """frame 60 shot netpair-j-lobby
# portrait 1 (where this selector starts): wait for the host's CLASSIC and its pick, fire, the name GUEST is kept (fire): done (no Control line)
wait log lobby_mode_classic max 15000
wait log lobby_seat_0_done max 15000
frame 30 joy1 fire pulse 30
frame 110 shot netpair-j-name
frame 120 joy1 fire pulse 30
wait log lockstep_begins max 12000"""
    return pre.replace('netpair-j-', prefix + '-')


def render_classic(k):
    head = CLASSIC_HEAD.format(side='HOST' if k == 0 else 'JOIN', side_doc=CLASSIC_SIDE_DOC[k])
    return render_steps(head, classic_pre(k), classic_plan(), k, 'netpair-ch' if k == 0 else 'netpair-cj', CLASSIC_LAST)


def extra_outputs():
    """[(file name, text)] of the quad and classic runs."""
    out = [('net_quad_host.txt', render_quad(0))] + [('net_quad_join%d.txt' % k, render_quad(k)) for k in (1, 2, 3)]
    return out + [('net_classic_host.txt', render_classic(0)), ('net_classic_join.txt', render_classic(1))]


def main():
    bad = False
    outputs = []
    for kind, side in (('pair', 'host'), ('pair', 'join'), ('relay', 'host'), ('relay', 'join'), ('end', 'host'), ('end', 'join')):
        outputs.append(('net_%s_%s.txt' % (kind, side), render(side, kind == 'relay', kind == 'end')))
    outputs += extra_outputs()
    for name, text in outputs:
        path = os.path.join(OUT, name)
        if '--check' in sys.argv:
            cur = open(path, newline='').read().replace('\r\n', '\n') if os.path.exists(path) else ''
            if cur != text:
                print('stale:', path)
                bad = True
        else:
            with open(path, 'w', newline='\n') as f:
                f.write(text)
            print('wrote', path, text.count('\n'), 'lines')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
