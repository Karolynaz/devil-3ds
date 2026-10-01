#include "platform/ctr/world_surface.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>

#include "platform/ctr/display.hpp"

namespace devilution {

namespace {

std::optional<OwnedSurface> WorldBuffer;
int WorldBufferLevel = -1;
bool WorldFrameValid = false;

void GiveWorldToPresenter()
{
	CtrWorldFrame frame;
	frame.pixels = WorldBuffer->at(0, 0);
	frame.pitch = WorldBuffer->pitch();
	frame.level = WorldBufferLevel;
	CTR_SetWorldFrame(frame);
}

} // namespace

const Surface &CtrWorldSurface()
{
	const int level = CtrWorldZoomLevel();
	if (!WorldBuffer || WorldBufferLevel != level) {
		WorldBuffer.emplace(CtrWorldWidthFor(level), CtrWorldHeightFor(level));
		WorldBufferLevel = level;
		// The old frame has another size. Wait for the next full frame.
		WorldFrameValid = false;
	}
	return *WorldBuffer;
}

void CtrWorldBeginFrame(const Surface &out)
{
	CtrWorldSurface();
	const int rows = std::min(out.h(), CtrScreenHeight);
	const int width = out.w();
	for (int y = 0; y < rows; ++y)
		std::memset(out.at(0, y), CtrWorldKeyIndex, width);
	WorldFrameValid = true;
	GiveWorldToPresenter();
}

void CtrWorldRepeatFrame()
{
	if (WorldFrameValid && WorldBuffer && WorldBufferLevel == CtrWorldZoomLevel())
		GiveWorldToPresenter();
}

bool CtrIsUiLayer(const Surface &out)
{
	return !WorldBuffer || out.begin() < WorldBuffer->begin() || out.begin() >= WorldBuffer->end();
}

} // namespace devilution
