#include "platform/ctr/world_surface.hpp"

#include <3ds.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include "platform/ctr/display.hpp"

namespace devilution {
namespace {
std::optional<OwnedSurface> WorldBuffer;
std::optional<OwnedSurface> RightBuffer;
bool WorldFrameValid = false;
bool StereoFrame = false;
bool RenderingRightEye = false;
int SpriteOffset = 0;
void GiveWorldToPresenter()
{
	CTR_SetWorldFrame({ WorldBuffer->at(0, 0), WorldBuffer->pitch(),
		StereoFrame ? RightBuffer->at(0, 0) : nullptr,
		StereoFrame ? RightBuffer->pitch() : 0 });
}
bool Contains(const Surface &buffer, const Surface &out)
{
	const auto address = reinterpret_cast<uintptr_t>(out.begin());
	return address >= reinterpret_cast<uintptr_t>(buffer.begin())
		&& address < reinterpret_cast<uintptr_t>(buffer.end());
}
} // namespace
const Surface &CtrWorldSurface()
{
	if (!WorldBuffer)
		WorldBuffer.emplace(CtrWorldWidth(), CtrWorldHeight());
	return *WorldBuffer;
}
const Surface &CtrWorldRightSurface()
{
	return *RightBuffer;
}
void CtrWorldBeginFrame(const Surface &out, bool allowStereo)
{
	CtrWorldSurface();
	RenderingRightEye = false;
	SpriteOffset = allowStereo ? CtrStereoOffsetFor(osGet3DSliderState()) : 0;
	StereoFrame = SpriteOffset != 0;
	if (StereoFrame && !RightBuffer)
		RightBuffer.emplace(CtrWorldWidth(), CtrWorldHeight());
	for (int y = 0; y < std::min(out.h(), CtrScreenHeight); ++y)
		std::memset(out.at(0, y), CtrWorldKeyIndex, out.w());
	WorldFrameValid = true;
	GiveWorldToPresenter();
}
void CtrWorldRepeatFrame()
{
	if (WorldFrameValid && WorldBuffer)
		GiveWorldToPresenter();
}
void CtrWorldClearFrame()
{
	WorldFrameValid = false;
	StereoFrame = false;
	CTR_SetWorldFrame({ nullptr, 0, nullptr, 0 });
}
bool CtrIsUiLayer(const Surface &out)
{
	return (!WorldBuffer || !Contains(*WorldBuffer, out))
		&& (!RightBuffer || !Contains(*RightBuffer, out));
}
bool CtrWorldHasRightEye() { return StereoFrame; }
bool CtrRenderingRightEye() { return RenderingRightEye; }
void CtrSetRenderingRightEye(bool right) { RenderingRightEye = right; }
int CtrStereoSpriteOffset() { return RenderingRightEye ? SpriteOffset : 0; }
} // namespace devilution
