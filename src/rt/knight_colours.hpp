// rt/knight_colours - a colour of its own for the second party knight in a creature fight (ROADMAP 8.4b, docs/MOONSTONE2.md 4.5).
// A creature scene has ONE knight colour triplet (pal[6..8] = the first fighter's class); the second knight would be the same blue.  The
// scenes' backgrounds never use some of the palette entries 16..31, so for a party fight the second knight gets a COPY of the knight cels
// (KN1..KN3, engine/celremap.hpp) whose class colours 6, 7, 8 are three such free entries, and the scene palette gets that knight's class
// triplet there.  Made once at the start of the fight (a few seconds of CPU during the fade-in, 60-85 KB chip while the fight lasts); no
// per-frame cost.  Nothing happens in a classic game or when no three entries are free (the knights then share the colour as before: the
// log says so).  The duel scene ($0C, the black knights own pal[9..11], the cels HE1..3 only use low entries) and the dragon scene ($14) search
// like the lairs; the dragon, balok and troll arenas also draw the fifth knight set (KN5: 29..31), which counts as used.  A dragon arena whose
// picture uses all of 16..28 but two entries (bg2, bg5a) has too few free entries and falls back.
// The knight figure is also drawn out of other cel sets: KN4 (weapons), KN5, and the creature sets that carry a knight (the ratmen's curse
// and grips, the Be's grip: a knight script switches to the creature cel list with the $C0 op, and a creature's job draws the knight it
// holds from its own set).  Every such set with class-colour pixels gets a copy too (best effort, one chip block each, celSkinCandidates);
// game/combat_script.cpp drawEvent picks the copy through rosterDrawSkin.  tests/test_skin_cover.py checks the coverage against the scripts.
#pragma once
#include <ace/types.h>

namespace rt {

// Called by the scene palette (rt/palette_glue.cpp) just before the fade-in, with the palette it built.
void partyColoursApply(UWORD *puwPalette, ULONG ulScene);
// The end of the party fight (rt/combat_pool.cpp): the copies are given back.
void partyColoursEnd();

}  // namespace rt
