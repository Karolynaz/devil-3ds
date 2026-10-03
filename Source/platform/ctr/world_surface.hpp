#pragma once
#include "engine/surface.hpp"
#include "platform/ctr/world_view.hpp"
namespace devilution {
const Surface &CtrWorldSurface();
const Surface &CtrWorldRightSurface();
void CtrWorldBeginFrame(const Surface &out, bool allowStereo);
void CtrWorldRepeatFrame();
bool CtrIsUiLayer(const Surface &out);
bool CtrWorldHasRightEye();
bool CtrRenderingRightEye();
void CtrSetRenderingRightEye(bool right);
int CtrStereoSpriteOffset();
} // namespace devilution
