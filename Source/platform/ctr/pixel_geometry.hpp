#pragma once
#include "engine/size.hpp"
namespace devilution {
// The encoded canvas is fixed; saved desktop/old 3DS resolutions cannot resize it.
constexpr Size CtrCanvasSize { 640, 480 };
// Native screen column -> first logical canvas column displaying that pixel.
constexpr int CtrNativeColumn(int x, int screenWidth) { return x * 640 / screenWidth; }
// Fixed inverse sampling: unlike scaling a local rectangle, its phase is global.
constexpr int CtrInverseColumn(int x, int screenWidth) { return ((x + 1) * screenWidth + 639) / 640 - 1; }
constexpr Size CtrEnlargeImage(Size source, Size bounds)
{
	if (source.width <= 0 || source.height <= 0) return { 0, 0 };
	const int xScale = bounds.width / source.width;
	const int yScale = bounds.height / source.height;
	const int scale = xScale < yScale ? xScale : yScale;
	return scale > 0 ? Size { source.width * scale, source.height * scale } : Size { 0, 0 };
}
constexpr Size CtrFitImage(Size source, Size bounds)
{
	if (source.width <= 0 || source.height <= 0 || bounds.width <= 0 || bounds.height <= 0) return { 0, 0 };
	if (source.width <= bounds.width && source.height <= bounds.height) return source;
	if (source.width * bounds.height > source.height * bounds.width)
		return { bounds.width, bounds.width * source.height / source.width > 0 ? bounds.width * source.height / source.width : 1 };
	return { bounds.height * source.width / source.height > 0 ? bounds.height * source.width / source.height : 1, bounds.height };
}
} // namespace devilution
