#pragma once
#include "engine/size.hpp"
namespace devilution {
// Native screen column -> first logical canvas column displaying that pixel.
constexpr int CtrNativeColumn(int x, int screenWidth) { return x * 640 / screenWidth; }
// Fixed inverse sampling: unlike scaling a local rectangle, its phase is global.
constexpr int CtrInverseColumn(int x, int screenWidth) { return ((x + 1) * screenWidth + 639) / 640 - 1; }
constexpr Size CtrFitImage(Size source, Size bounds)
{
	if (source.width <= 0 || source.height <= 0 || bounds.width <= 0 || bounds.height <= 0) return { 0, 0 };
	if (source.width <= bounds.width && source.height <= bounds.height) return source;
	if (source.width * bounds.height > source.height * bounds.width)
		return { bounds.width, bounds.width * source.height / source.width > 0 ? bounds.width * source.height / source.width : 1 };
	return { bounds.height * source.width / source.height > 0 ? bounds.height * source.width / source.height : 1, bounds.height };
}
} // namespace devilution
