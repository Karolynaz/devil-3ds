#include "platform/ctr/world_surface.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include "platform/ctr/display.hpp"

namespace devilution {
namespace {
std::optional<OwnedSurface> WorldBuffer;
bool WorldFrameValid = false;
void GiveWorldToPresenter()
{
	CTR_SetWorldFrame({ WorldBuffer->at(0, 0), WorldBuffer->pitch(), nullptr, 0 });
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
void CtrWorldBeginFrame(const Surface &out)
{
	CtrWorldSurface();
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
	CTR_SetWorldFrame({ nullptr, 0, nullptr, 0 });
}
bool CtrIsUiLayer(const Surface &out)
{
	return !WorldBuffer || !Contains(*WorldBuffer, out);
}
int CtrStereoSpriteOffset() { return 0; }
} // namespace devilution
