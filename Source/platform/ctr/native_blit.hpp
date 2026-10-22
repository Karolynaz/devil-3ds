#pragma once

#include <algorithm>
#include "engine/surface.hpp"
#include "platform/ctr/pixel_geometry.hpp"

namespace devilution {
/** Encode native 400-wide screen pixels on the global canvas sampling grid.
 * Unlike scaling a local rectangle, every column survives at every position.
 */
inline void CtrBlitTopNative(const Surface &out, const Surface &image, Point position)
{
	const int firstX = CtrNativeColumn(std::max(position.x, 0), 400);
	const int lastX = CtrNativeColumn(std::min(position.x + image.w(), 400), 400);
	for (int y = std::max(position.y, 0); y < std::min(position.y + image.h(), 240); ++y) {
		const uint8_t *src = image.at(0, y - position.y);
		uint8_t *dst = out.at(0, y);
		for (int x = firstX; x < lastX; ++x)
			dst[x] = src[CtrInverseColumn(x, 400) - position.x];
	}
}
} // namespace devilution
