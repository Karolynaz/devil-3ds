#include "engine/surface.hpp"

#include <cstdint>
#ifdef __3DS__
#include <array>
#endif
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

template <bool SkipColorIndexZero>
static void ScaleBlit(const Surface &dst, const Surface &src, SDL_Rect srcRect, SDL_Rect dstRect)
{
	if (srcRect.w <= 0 || srcRect.h <= 0 || dstRect.w <= 0 || dstRect.h <= 0)
		return;

	int x0 = std::max<int>(dstRect.x, 0);
	int y0 = std::max<int>(dstRect.y, 0);
	int x1 = std::min<int>(dstRect.x + dstRect.w, dst.region.w);
	int y1 = std::min<int>(dstRect.y + dstRect.h, dst.region.h);
	if (x0 >= x1 || y0 >= y1)
		return;

#ifdef __3DS__
	std::array<int, 640> sourceColumns;
	const bool cachedColumns = x1 - x0 <= static_cast<int>(sourceColumns.size());
	if (cachedColumns) {
		for (int x = x0; x < x1; ++x)
			sourceColumns[x - x0] = srcRect.x + ((x - dstRect.x) * srcRect.w) / dstRect.w;
	}
#endif

	for (int y = y0; y < y1; ++y) {
		int sy = srcRect.y + ((y - dstRect.y) * srcRect.h) / dstRect.h;
		const uint8_t *srcRow = src.at(0, sy);
		uint8_t *dstRow = dst.at(0, y);
		for (int x = x0; x < x1; ++x) {
#ifdef __3DS__
			const int sx = cachedColumns ? sourceColumns[x - x0] : srcRect.x + ((x - dstRect.x) * srcRect.w) / dstRect.w;
#else
			const int sx = srcRect.x + ((x - dstRect.x) * srcRect.w) / dstRect.w;
#endif
			const uint8_t pixel = srcRow[sx];
			if (!SkipColorIndexZero || pixel != 0)
				dstRow[x] = pixel;
		}
	}
}

void Surface::ScaleBlitFrom(const Surface &src, SDL_Rect srcRect, SDL_Rect dstRect) const
{
	ScaleBlit</*SkipColorIndexZero=*/false>(*this, src, srcRect, dstRect);
}

#ifdef __3DS__
void Surface::ScaleBlitFromPreservingDownscale(const Surface &src, SDL_Rect srcRect, SDL_Rect dstRect) const
{
	if (srcRect.w <= 0 || srcRect.h <= 0 || dstRect.w <= 0 || dstRect.h <= 0)
		return;
	if (dstRect.w < srcRect.w || dstRect.h < srcRect.h) {
		ScaleBlit</*SkipColorIndexZero=*/false>(*this, src, srcRect, dstRect);
		return;
	}

	const int x0 = std::max<int>(dstRect.x, 0);
	const int y0 = std::max<int>(dstRect.y, 0);
	const int x1 = std::min<int>(dstRect.x + dstRect.w, region.w);
	const int y1 = std::min<int>(dstRect.y + dstRect.h, region.h);
	if (x0 >= x1 || y0 >= y1)
		return;

	// Use the inverse of floor-based downsampling: source index is
	// ceil((destinationIndex + 1) * sourceSize / destinationSize) - 1.
	// Thus a later sample at floor(sourceIndex * destinationSize / sourceSize)
	// maps back to that same source index.
#ifdef __3DS__
	std::array<int, 640> sourceColumns;
	const bool cachedColumns = x1 - x0 <= static_cast<int>(sourceColumns.size());
	if (cachedColumns) {
		for (int x = x0; x < x1; ++x)
			sourceColumns[x - x0] = srcRect.x + ((x - dstRect.x + 1) * srcRect.w + dstRect.w - 1) / dstRect.w - 1;
	}
#endif

	for (int y = y0; y < y1; ++y) {
		const int relativeY = y - dstRect.y;
		const int sy = srcRect.y + ((relativeY + 1) * srcRect.h + dstRect.h - 1) / dstRect.h - 1;
		const uint8_t *srcRow = src.at(0, sy);
		uint8_t *dstRow = &(*this)[{ 0, y }];
		for (int x = x0; x < x1; ++x) {
			const int relativeX = x - dstRect.x;
			const int sx = cachedColumns ? sourceColumns[x - x0] : srcRect.x + ((relativeX + 1) * srcRect.w + dstRect.w - 1) / dstRect.w - 1;
			dstRow[x] = srcRow[sx];
		}
	}
}
#endif

void Surface::ScaleBlitFromSkipColorIndexZero(const Surface &src, SDL_Rect srcRect, SDL_Rect dstRect) const
{
	ScaleBlit</*SkipColorIndexZero=*/true>(*this, src, srcRect, dstRect);
}

} // namespace devilution
