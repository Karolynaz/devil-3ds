#pragma once
#include "engine/surface.hpp"
#include "platform/ctr/world_view.hpp"
namespace devilution {
const Surface &CtrWorldSurface();
void CtrWorldBeginFrame(const Surface &out);
void CtrWorldRepeatFrame();
/** Forget the scene when replacing the keyed canvas with a menu, movie or loading screen. */
void CtrWorldClearFrame();
bool CtrIsUiLayer(const Surface &out);
int CtrStereoSpriteOffset();
} // namespace devilution
