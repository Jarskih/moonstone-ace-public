// game/party - the co-op mode switch and the party's controllers (ROADMAP 8.2, docs/MOONSTONE2.md section 6 "Mode switch and
// parity").  Pure: no ACE, no hardware; src/game/party.cpp holds g_party and the helpers.
//
// g_party is THE switch of Moonstone 2: every co-op code path tests g_party.active and keeps the original branch for
// !active, so classic mode stays bit-identical (parity tests, boot references).  It is zero at start-up (BSS: no constructor
// runs), i.e. inactive; the title menu turns it on ("Players ... Coop 2", src/game/scene_menu.cpp) and the knight screen
// fills the controller of every member.  CMake MS_COOP=OFF removes the menu entry, so the switch can never be turned on.
//
// Party members are the human knight records 0..n-1 (mogKnights[i], as the original's human players; the black knights are the
// records after them, the dragon stays record 4).  Member i is steered by the controller aubPad[i] (ms::PadSource).
#pragma once
#include <stdint.h>

#include "engine/pad.hpp"

namespace ms { namespace game {

constexpr uint8_t PARTY_MAX = 4;               // the engine is sized for four knights (records 0..3)
constexpr uint8_t PARTY_COOP_MAX = 2;          // what the menu offers today (owner 2026-10-07: two knights first)
constexpr uint8_t PARTY_FOCUS_ANY = 0xFF;      // ubFocus: the player stick is every controller OR-ed (menus, knight screen)
constexpr uint8_t PARTY_FOCUS_TURN = 0xFE;     // ubFocus: the player stick is the current knight's controller (map, fights)
constexpr uint8_t PARTY_MEMBER_NONE = 0xFF;

struct PartyConfig {
	bool active;                  // co-op mode on; false = classic (the original rules, bit-identical)
	uint8_t n;                    // knights in the party (1..PARTY_MAX), records 0..n-1
	uint8_t aubPad[PARTY_MAX];    // controller of member i (ms::PadSource); PAD_NONE = not bound yet
	uint8_t ubFocus;              // whose controller is the "player stick" (the LAB_0630 word): PARTY_FOCUS_* or a member index
};

extern PartyConfig g_party;

// Back to classic: inactive, no members, pads unbound, focus ANY.
void partyReset(PartyConfig &p);
// Co-op with n members (clamped to 1..PARTY_MAX), each on its default controller, focus ANY (the knight screen follows).
void partyStart(PartyConfig &p, uint8_t ubN);
// The controller member i gets unless a player picks another: 0 -> joystick 1, 1 -> joystick 2, 2 -> arrows, 3 -> WASD.
uint8_t partyDefaultPad(uint8_t ubMember);
// True when one of the members 0..ubBefore-1 already uses the controller (two knights never share a controller).
bool partyPadTaken(const PartyConfig &p, uint8_t ubBefore, uint8_t ubPad);
// The next selectable controller (ms::PAD_SELECTABLE_COUNT of them) from ubFrom in direction swDir (+1 / -1, wrapping)
// that no member before ubMember uses; ubFrom itself when there is none.
uint8_t partyNextPad(const PartyConfig &p, uint8_t ubMember, uint8_t ubFrom, int8_t sbDir);
// The first free controller for a member: its default when free, else the next free one.
uint8_t partyFirstFreePad(const PartyConfig &p, uint8_t ubMember);
// The controller word of member i (0 when it is not a member).
uint16_t partyPadBits(const PartyConfig &p, const PadFrame &f, uint8_t ubMember);
// The word the game's "player stick" (port 1 word, LAB_0630) carries in co-op, by ubFocus: every controller (ANY), the
// current knight's (TURN: ubCurMember = the record index of the current knight, PARTY_MEMBER_NONE when it is no member: then
// the port 1 joystick as in classic) or one member's.
uint16_t partyStick(const PartyConfig &p, const PadFrame &f, uint8_t ubCurMember);

}}  // namespace ms::game
