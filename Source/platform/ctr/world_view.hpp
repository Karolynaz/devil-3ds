#pragma once

#include <cstdint>

namespace devilution {
constexpr int CtrScreenWidth = 400;
constexpr int CtrScreenHeight = 240;
// Player sprites extend above their tile anchor. Center the torso, not the feet.
constexpr int CtrCameraVerticalOffset = 28;
constexpr int CtrWorldWidth() { return 400; }
constexpr int CtrWorldHeight() { return 240; }
constexpr int CtrUiToWorldX(int x, int uiWidth)
{
	// The presenter samples UI column floor(screenX * uiWidth / 400). This is
	// its exact inverse for a UI pixel: ceil((x + 1) * 400 / uiWidth) - 1.
	// Using x * 400 / uiWidth instead selects the preceding world pixel for
	// many columns on the 640-wide canvas.
	const int numerator = (x + 1) * CtrWorldWidth() - 1;
	return numerator >= 0 ? numerator / uiWidth : -((-numerator + uiWidth - 1) / uiWidth);
}
// Reserved UI keys; world pixels are never interpreted as keys.
constexpr uint8_t CtrWorldKeyIndex = 2;
constexpr uint8_t CtrWorldDimKeyIndex = 1;
constexpr int CtrDimShift = 1;
constexpr uint32_t CtrDimRedBlue(uint32_t c) { return (c >> CtrDimShift) & 0x00FF00FFu; }
constexpr uint32_t CtrDimChannel(uint32_t c) { return c >> CtrDimShift; }
} // namespace devilution
