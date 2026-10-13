// rt/party_fight - PlaceOps::pfnPartyFightOpen / pfnPartyFightClose / pfnPartyValleySettle (ROADMAP 8.4, rt/party_fight.cpp).
#pragma once

namespace rt {

bool partyFightOpen(void *pCtx);      // seeds and opens the fight roster from the co-op party; true = a party fight runs (false in a classic game)
void partyFightClose(void *pCtx);     // closes it, gives the job pool back
bool partyValleySettle(void *pCtx);   // while open, after the fight: every fallen member pays the guardian's penalty; true = the whole party lost

}  // namespace rt
