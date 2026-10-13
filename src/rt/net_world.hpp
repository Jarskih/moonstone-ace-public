// rt/net_world - the live game as a NetWorldImage (ROADMAP M12, rejoin, docs/NETPLAY.md 4.5): reads the cells into the image and writes an image back.
// The pure halves are game/net_world (image, byte form, hash, pointer normalisation) and game/net_rejoin (what a returning player does to the game);
// this file only knows where the cells are.
#pragma once
#include <ace/types.h>

#include "game/net_world.hpp"

namespace rt {

// Copies the world into the image: net_state's capture of the hashed part (knights, lairs, scalars ...), then the names, the extras, the party map, the
// black-knight pool, and the address tables the pointers are normalised with (bases of the arrays, the name texts, the owned data objects).
void netWorldCapture(ms::game::NetWorldImage &sImage, ULONG ulTick, ULONG ulClock);

// Writes the image into the cells (the receiver of a snapshot).  The creature records are all marked inactive.  Pointers in the image are already this
// machine's addresses (netWorldDecode rebuilt them over a capture).  The map scene is NOT rebuilt here: the caller runs SECSTRT_36 and enters the map.
void netWorldApply(const ms::game::NetWorldImage &sImage);

// The state a rejoin leaves behind on a machine that stays in the game (the host and the other clients): the map scene starts over at the turn, so the
// jobs (the dragon's flight) and the map colour jobs are gone and the dragon takes off again.  State only: no fade, no waiting.  The snapshot has the same
// changes made to the image (netWorldLandDragon).
void netWorldRestartMap();
// The same change on an image: the dragon is not flying (its job does not exist on the machine that loads it).
void netWorldLandDragon(ms::game::NetWorldImage &sImage);

}  // namespace rt
