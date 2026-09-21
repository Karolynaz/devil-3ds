#include "engine/surface.hpp"

#include <cstdint>
#include <cstring>

namespace devilution {

namespace {

template <bool SkipColorIndexZero>
void SurfaceBlit(const Surface &src, SDL_Rect srcRect, const Surface &dst, Point dstPosition)
{
	// We do not use `SDL_BlitSurface` here because the palettes may be different objects
	// and SDL would attempt to map them.

	dst.Clip(&srcRect, &dstPosition);
	if (srcRect.w <= 0 || srcRect.h <= 0)
		return;

	const std::uint8_t *srcBuf = src.at(srcRect.x, srcRect.y);
	const auto srcPitch = src.pitch();
	std::uint8_t *dstBuf = &dst[dstPosition];
	const auto dstPitch = dst.pitch();

	for (unsigned h = srcRect.h; h != 0; --h) {
		if (SkipColorIndexZero) {
			for (unsigned w = srcRect.w; w != 0; --w) {
				if (*srcBuf != 0)
					*dstBuf = *srcBuf;
				++srcBuf, ++dstBuf;
			}
			srcBuf += srcPitch - srcRect.w;
			dstBuf += dstPitch - srcRect.w;
		} else {
			std::memcpy(dstBuf, srcBuf, srcRect.w);
			srcBuf += srcPitch;
			dstBuf += dstPitch;
		}
	}
}

} // namespace

void Surface::BlitFrom(const Surface &src, SDL_Rect srcRect, Point targetPosition) const
{
	SurfaceBlit</*SkipColorIndexZero=*/false>(src, srcRect, *this, targetPosition);
}

void Surface::BlitFromSkipColorIndexZero(const Surface &src, SDL_Rect srcRect, Point targetPosition) const
{
	SurfaceBlit</*SkipColorIndexZero=*/true>(src, srcRect, *this, targetPosition);
}

void Surface::ScaleBlitFrom(const Surface &src, SDL_Rect srcRect, SDL_Rect dstRect) const
{
	if (srcRect.w <= 0 || srcRect.h <= 0 || dstRect.w <= 0 || dstRect.h <= 0)
		return;

	int x0 = std::max<int>(dstRect.x, 0);
	int y0 = std::max<int>(dstRect.y, 0);
	int x1 = std::min<int>(dstRect.x + dstRect.w, region.w);
	int y1 = std::min<int>(dstRect.y + dstRect.h, region.h);
	if (x0 >= x1 || y0 >= y1)
		return;

	for (int y = y0; y < y1; ++y) {
		int sy = srcRect.y + ((y - dstRect.y) * srcRect.h) / dstRect.h;
		const uint8_t *srcRow = src.at(0, sy);
		uint8_t *dstRow = at(0, y);
		for (int x = x0; x < x1; ++x) {
			int sx = srcRect.x + ((x - dstRect.x) * srcRect.w) / dstRect.w;
			dstRow[x] = srcRow[sx];
		}
	}
}

} // namespace devilution
