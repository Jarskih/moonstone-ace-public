"""sounds.py -- which sample bank a sound id needs (ROADMAP 9.8a1, docs/MONSTER_KIT.md 1.8).

`$A4 id` and the `$B0` sound pickers start sequence `id` of the mog synth (rt::sfxRequest -> rtSfxSynth(seq = id, channel);
src/rt/sfx.cpp).  A sequence selects instruments (`$D0 n`, also reached through `$B0` calls and `$D4` jumps); an instrument's
sample sits in a fixed bank buffer (tools/gen_synth_tables.py BANK_RULES, LAB_0FD4):

    wave  the nine built-in waveforms (always there)           C7  LAB_05C7 the knights' bank (loaded with every fight)
    C8    LAB_05C8 the creature bank: ONE of the creature .a files (Be, Balok, Dragon, Demon, Troll, Trogg, Mudmen)
    C9    LAB_05C9 the campaign bank (resident from LAB_0AA7)    CA  LAB_05CA the wizard bank    CB  LAB_05CB the ratmen bank

So an id whose sequences touch only wave/C7/C9 plays in every fight, an id with C8 instruments sounds right only with the
creature bank that holds those samples (the offsets are per bank file), CB only with the ratmen bank, CA never in a fight.

    banks_of(id)  -> sorted bank names;   instruments_of(id) -> sorted instrument indices
Needs the original mog hunk (reference/): tools/gen_synth_tables.build() reads it.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import gen_synth_tables as G  # noqa: E402

BANK_NAMES = {v: k for k, v in G.NBANK.items()}
SEQ_COUNT = G.SEQ_COUNT
# operand bytes of the synth commands ($80 + 4n), src/engine/synth.cpp:241-340; $98 is variable (count + count bytes)
OPERANDS = {0x80: 1, 0x84: 1, 0x88: 0, 0x8C: 1, 0x90: 0, 0x94: 1, 0x9C: 1, 0xA0: 1, 0xA4: 1, 0xA8: 1, 0xAC: 0, 0xB0: 1,
            0xB4: 0, 0xB8: 1, 0xBC: 1, 0xC0: 1, 0xC4: 0, 0xC8: 1, 0xCC: 0, 0xD0: 1, 0xD4: 1}

_cache = {}


def tables():
    if 'blob' not in _cache:
        blob, insts, _wave = G.build()
        _cache['blob'], _cache['insts'] = blob, insts
    return _cache['blob'], _cache['insts']


def seq_start(blob, seq):
    o = G.SEQ_TABLE - G.BLOB_BASE + 4 * seq
    return int.from_bytes(blob[o:o + 4], 'big')


def instruments_of(seq, _seen=None):
    """Instrument indices a sequence can select: a linear scan of its stream, following `$B0` calls and `$D4` jumps; stops at
    `$AC` (stop), `$88` (restart), `$B4` (return) and at a byte that is no command."""
    blob, _ = tables()
    seen = _seen if _seen is not None else set()
    if seq in seen or seq <= 0 or seq >= SEQ_COUNT:
        return set()
    seen.add(seq)
    out = set()
    pos = seq_start(blob, seq) - G.BLOB_BASE
    while 0 <= pos < len(blob):
        b = blob[pos]
        pos += 1
        if b < 0x80:
            continue                                    # a note
        if b not in OPERANDS and b != 0x98:
            break
        if b == 0x98:
            n = blob[pos]
            pos += 1 + n
            continue
        arg = blob[pos] if OPERANDS[b] and pos < len(blob) else None
        pos += OPERANDS[b]
        if b == 0xD0:
            out.add(arg)
        elif b == 0xB0:
            out |= instruments_of(arg, seen)
        elif b == 0xD4:
            out |= instruments_of(arg, seen)
            break
        elif b in (0xAC, 0x88, 0xB4):
            break
    return out


def banks_of(seq):
    _, insts = tables()
    return sorted({BANK_NAMES[insts[i][3]] for i in instruments_of(seq) if i < len(insts)})


def usable_with(seq, bank_file):
    """True when sequence `seq` sounds right in a fight whose creature bank is `bank_file` ('Be.a', 'Ra.a', ...; None = unknown):
    wave / C7 / C9 are always there, C8 needs a creature bank at all, CB only the ratmen bank Ra.a, CA never.  (That the C8
    samples are the RIGHT ones is up to the creature: ids used by the original creature of that bank are right by construction.)"""
    banks = set(banks_of(seq)) - {'wave', 'C7', 'C9'}
    if 'C8' in banks and not bank_file:
        return False
    banks.discard('C8')
    if 'CB' in banks and bank_file != 'Ra.a':
        return False
    banks.discard('CB')
    return not banks
