// rt/net_stub - the network seams of the Multiplayer screen (game/net_menu.hpp NetSeam) and of the lobby = the knight screen
// (game/scene_lobby.hpp LobbySeam): the real session (rt/net_session) or "no TCP/IP stack".  Under MS_AUTOPLAY the `poke net...` hooks fake a
// stack and a room, so the screens can be exercised headless (tests/boot/play_netmenu.txt).
#pragma once
#include <ace/types.h>

#include "game/net_find.hpp"
#include "game/net_menu.hpp"
#include "game/scene_lobby.hpp"

namespace rt {

// The seams the screens are wired to.
const ms::game::NetSeam &netSeam();
const ms::game::LobbySeam &lobbySeam();
const ms::game::FindSeam &findSeam();       // the game browser (FIND GAME): always the real lobby client (rt/net_find), never faked

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
// `poke <name> <value>` hooks (rt/autoplay_game.cpp): netfake 1 = a stack is present (host works, joins get an answer), netpeers N =
// 0..3 clients in the fake host's lobby, each already picked (BOB portrait 3, CAROL 2, DAVE 1), netjoin N = the join's outcome (0 connected,
// 1 no answer, 2 wrong version, 3 stays connecting), netseated 1 = the fake host seats the joining player (the lobby opens), netstarted 1 =
// the fake host's start message arrives in the client's lobby, netaction N = a button from outside (1 host, 2 connect, 3 the lobby's START).
// False = not a net hook / bad value.
bool netFakePoke(const char *szName, ULONG ulValue);
// `poke menuact N` (rt/scene_menu.cpp): the main menu / the title menu takes a row without cursor steps, as fire on that row; 1 = Multiplayer,
// 2 = Settings (the Options screen), 3 = Local + Select Knight, 4 = Local + Practice (docs/AUTOPLAY.md).  Taken by the menu loops; false = bad value.
enum MenuAct : uint8_t { MENUACT_NONE = 0, MENUACT_MULTIPLAYER = 1, MENUACT_SETTINGS = 2, MENUACT_SELECT = 3, MENUACT_PRACTICE = 4 };
bool menuActionPoke(ULONG ulValue);
#else
inline bool netFakePoke(const char *, ULONG) { return false; }
#endif

}  // namespace rt
