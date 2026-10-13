// engine/netroom - see include/engine/netroom.hpp (docs/NETPLAY.md 4.2b).  New code, no original label.
#include "engine/netroom.hpp"

#include "engine/pad.hpp"

namespace ms {

namespace {

void clearName(char (&ac)[NET_NAME_LEN]) {
	for(uint8_t i = 0; i < NET_NAME_LEN; ++i) {
		ac[i] = 0;
	}
}

void copyName(char (&acDst)[NET_NAME_LEN], const char (&acSrc)[NET_NAME_LEN]) {
	for(uint8_t i = 0; i < NET_NAME_LEN; ++i) {
		acDst[i] = acSrc[i];
	}
}

bool isSameName(const char (&a)[NET_NAME_LEN], const char (&b)[NET_NAME_LEN]) {
	for(uint8_t i = 0; i < NET_NAME_LEN; ++i) {
		if(a[i] != b[i]) {
			return false;
		}
	}
	return true;
}

bool isSameSeat(const NetSeatRec &a, const NetSeatRec &b) {
	return a.ubState == b.ubState && a.ubSel == b.ubSel && a.ubPad == b.ubPad && isSameName(a.acName, b.acName);
}

// Whether a seat other than ubExceptSeat has its selector on the portrait (browsing or picked).
bool isStoodOn(const NetRoom &r, uint8_t ubPortrait, uint8_t ubExceptSeat) {
	for(uint8_t i = 0; i < ROOM_SEATS; ++i) {
		if(i != ubExceptSeat && r.aSeat[i].ubState != NET_SEAT_FREE && r.aSeat[i].ubSel == ubPortrait) {
			return true;
		}
	}
	return false;
}

void freeSeat(NetSeatRec &s) {
	s.ubState = NET_SEAT_FREE;
	s.ubSel = 0;
	s.ubPad = 0xFF;
	clearName(s.acName);
}

}  // namespace

void roomInit(NetRoom &r, const char *szHostName) {
	r.ubMode = NET_MODE_COOP;
	for(uint8_t i = 0; i < ROOM_SEATS; ++i) {
		freeSeat(r.aSeat[i]);
	}
	roomJoin(r, 0, szHostName);
}

bool roomJoin(NetRoom &r, uint8_t ubSeat, const char *szName) {
	if(ubSeat >= ROOM_SEATS || r.aSeat[ubSeat].ubState != NET_SEAT_FREE) {
		return false;
	}
	NetSeatRec &s = r.aSeat[ubSeat];
	netNameSet(s.acName, szName);
	if(s.acName[0] == 0) {
		netNameSet(s.acName, "PLAYER");
	}
	s.ubState = NET_SEAT_BROWSE;
	s.ubPad = 0xFF;
	s.ubSel = roomFirstUnused(r, ubSeat);
	return true;
}

void roomLeave(NetRoom &r, uint8_t ubSeat) {
	if(ubSeat == 0 || ubSeat >= ROOM_SEATS) {
		return;
	}
	freeSeat(r.aSeat[ubSeat]);
}

uint8_t roomJoined(const NetRoom &r) {
	uint8_t ubCount = 0;
	for(uint8_t i = 0; i < ROOM_SEATS; ++i) {
		ubCount = static_cast<uint8_t>(ubCount + (r.aSeat[i].ubState != NET_SEAT_FREE ? 1 : 0));
	}
	return ubCount;
}

bool roomIsJoined(const NetRoom &r, uint8_t ubSeat) {
	return ubSeat < ROOM_SEATS && r.aSeat[ubSeat].ubState != NET_SEAT_FREE;
}

uint8_t roomHolder(const NetRoom &r, uint8_t ubPortrait) {
	for(uint8_t i = 0; i < ROOM_SEATS; ++i) {
		if(r.aSeat[i].ubState >= NET_SEAT_CLAIM && r.aSeat[i].ubSel == ubPortrait) {
			return i;
		}
	}
	return ROOM_SEATS;
}

bool roomIsHeld(const NetRoom &r, uint8_t ubPortrait, uint8_t ubExceptSeat) {
	const uint8_t ubHolder = roomHolder(r, ubPortrait);
	return ubHolder < ROOM_SEATS && ubHolder != ubExceptSeat;
}

uint8_t roomFirstFree(const NetRoom &r, uint8_t ubFrom, uint8_t ubSeat) {
	for(uint8_t k = 0; k < NET_PORTRAITS; ++k) {
		const uint8_t ubPortrait = static_cast<uint8_t>((ubFrom + k) % NET_PORTRAITS);
		if(!roomIsHeld(r, ubPortrait, ubSeat)) {
			return ubPortrait;
		}
	}
	return static_cast<uint8_t>(ubFrom % NET_PORTRAITS);   // cannot happen: at most three other seats hold a portrait
}

uint8_t roomFirstUnused(const NetRoom &r, uint8_t ubSeat) {
	for(uint8_t ubPortrait = 0; ubPortrait < NET_PORTRAITS; ++ubPortrait) {
		if(!isStoodOn(r, ubPortrait, ubSeat)) {
			return ubPortrait;
		}
	}
	return roomFirstFree(r, 0, ubSeat);   // all four are browsed: the lowest one nobody has picked
}

uint8_t roomStep(const NetRoom &r, uint8_t ubFrom, int8_t sbDir, uint8_t ubSeat) {
	int16_t sw = ubFrom;
	for(;;) {
		sw = static_cast<int16_t>(sw + (sbDir < 0 ? -1 : 1));
		if(sw < 0 || sw >= NET_PORTRAITS) {
			return ubFrom;
		}
		if(!roomIsHeld(r, static_cast<uint8_t>(sw), ubSeat)) {
			return static_cast<uint8_t>(sw);
		}
	}
}

bool roomApplyPick(NetRoom &r, uint8_t ubSeat, const NetLobbyPick &p) {
	if(!roomIsJoined(r, ubSeat) || p.ubSel >= NET_PORTRAITS) {
		return false;
	}
	NetSeatRec &s = r.aSeat[ubSeat];
	const NetSeatRec sOld = s;
	switch(p.ubStage) {
		case NET_PICK_BROWSE:
			s.ubState = NET_SEAT_BROWSE;       // lets go of a claimed portrait
			s.ubPad = 0xFF;
			s.ubSel = roomIsHeld(r, p.ubSel, ubSeat) ? roomFirstFree(r, p.ubSel, ubSeat) : p.ubSel;
			break;
		case NET_PICK_CLAIM:
			if(s.ubState == NET_SEAT_DONE || (s.ubState == NET_SEAT_CLAIM && s.ubSel == p.ubSel)) {
				break;                          // nothing new
			}
			if(roomIsHeld(r, p.ubSel, ubSeat)) {   // the race is lost: back to browsing, on a free portrait
				s.ubState = NET_SEAT_BROWSE;
				s.ubSel = roomFirstFree(r, p.ubSel, ubSeat);
			} else {
				s.ubState = NET_SEAT_CLAIM;
				s.ubSel = p.ubSel;
				for(uint8_t i = 0; i < ROOM_SEATS; ++i) {   // everybody who was browsing this knight is moved on to the next unpicked one
					NetSeatRec &o = r.aSeat[i];
					if(i != ubSeat && o.ubState == NET_SEAT_BROWSE && o.ubSel == p.ubSel) {
						o.ubSel = roomFirstFree(r, p.ubSel, i);
					}
				}
			}
			s.ubPad = 0xFF;
			break;
		case NET_PICK_DONE:
			if(s.ubState != NET_SEAT_CLAIM || s.ubSel != p.ubSel || p.ubPad >= PAD_SELECTABLE_COUNT || p.acName[0] == 0) {
				break;                          // a done without its claim (lost the race, or sent twice), or a controller that cannot be chosen
			}
			s.ubState = NET_SEAT_DONE;
			s.ubPad = p.ubPad;
			copyName(s.acName, p.acName);
			break;
		default:
			break;
	}
	return !isSameSeat(sOld, s);
}

bool roomSetMode(NetRoom &r, uint8_t ubMode) {
	if(ubMode > NET_MODE_COOP || r.ubMode == ubMode) {
		return false;
	}
	r.ubMode = ubMode;
	return true;
}

bool roomAllDone(const NetRoom &r) {
	for(uint8_t i = 0; i < ROOM_SEATS; ++i) {
		if(r.aSeat[i].ubState != NET_SEAT_FREE && r.aSeat[i].ubState != NET_SEAT_DONE) {
			return false;
		}
	}
	return true;
}

bool roomStartEnabled(const NetRoom &r) {
	return roomJoined(r) >= 2 && roomAllDone(r);
}

void roomToState(const NetRoom &r, uint8_t ubSeat, NetLobbyState &out) {
	out.ubMode = r.ubMode;
	out.ubYourSeat = ubSeat;
	for(uint8_t i = 0; i < ROOM_SEATS; ++i) {
		out.aSeats[i] = r.aSeat[i];
	}
}

bool roomEqual(const NetRoom &a, const NetRoom &b) {
	if(a.ubMode != b.ubMode) {
		return false;
	}
	for(uint8_t i = 0; i < ROOM_SEATS; ++i) {
		if(!isSameSeat(a.aSeat[i], b.aSeat[i])) {
			return false;
		}
	}
	return true;
}

bool roomFromState(NetRoom &r, const NetLobbyState &s) {
	NetRoom n;
	n.ubMode = s.ubMode;
	for(uint8_t i = 0; i < ROOM_SEATS; ++i) {
		n.aSeat[i] = s.aSeats[i];
	}
	const bool isChanged = !roomEqual(r, n);
	r = n;
	return isChanged;
}

uint8_t roomNodes(const NetRoom &r, uint8_t (&aubSeatOfNode)[ROOM_SEATS]) {
	uint8_t ubNodes = 0;
	for(uint8_t i = 0; i < ROOM_SEATS; ++i) {
		aubSeatOfNode[i] = 0xFF;
		if(r.aSeat[i].ubState != NET_SEAT_FREE) {
			aubSeatOfNode[ubNodes++] = i;
		}
	}
	return ubNodes;
}

}  // namespace ms
