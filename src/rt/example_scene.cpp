// rt/example_scene - the rt half of the "How to add a scene" example (src/game/scenes/example.cpp, docs/GAME_FLOW.md,
// ROADMAP 9.2a).  Built only with -DMS_EXAMPLE_SCENE=ON.  Shows one .piv picture from disk with the game's own primitives:
// the decode target LAB_05C0 (rt_mog_set_planes), the file load through the staging buffer LAB_05C2 (rt_mog_pic_file), the
// copy to both screens (LAB_0418) and the fade to the picture's palette (LAB_0D2B).
#if defined(MS_EXAMPLE_SCENE) && MS_EXAMPLE_SCENE
#include <stdint.h>

#include "rt/prim.hpp"

extern "C" {
uint32_t rtLoadPicFile_mog(const char *szName, uint8_t *pBuf);      // src/rt/loaders.cpp (rt_mog_pic_file)
extern uint32_t mogBackground, mogPicScreen;                        // LAB_05C0, LAB_05C2: the buffer addresses
extern uint8_t mogPicPalette[];                                     // LAB_0D2B: the palette of the last picture
}

void rtExampleShowPicture(const char *szFile) {
	rt::setDrawTarget(mogBackground);                                 // decode into LAB_05C0
	rtLoadPicFile_mog(szFile, reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(mogPicScreen)));
	rtMogBlitBothC();                                                // LAB_05C0 -> both screens
	rtMogFadeToC(reinterpret_cast<const uint16_t *>(mogPicPalette));  // fade in to the picture's colours
}
#endif
