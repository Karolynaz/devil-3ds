#pragma once

#include <cstdint>

namespace devilution {
constexpr int CtrScreenWidth = 400;
constexpr int CtrScreenHeight = 240;
// Player sprites extend above their tile anchor. Center the torso, not the feet.
constexpr int CtrCameraVerticalOffset = 28;
constexpr int CtrWorldWidth() { return 640; }
constexpr int CtrWorldHeight() { return 240; }
constexpr int CtrUiToWorldX(int x, int uiWidth) { return x * CtrWorldWidth() / uiWidth; }
// Reserved UI keys; world pixels are never interpreted as keys.
constexpr uint8_t CtrWorldKeyIndex = 2;
constexpr uint8_t CtrWorldDimKeyIndex = 1;
constexpr int CtrDimShift = 1;
constexpr uint32_t CtrDimRedBlue(uint32_t c) { return (c >> CtrDimShift) & 0x00FF00FFu; }
constexpr uint32_t CtrDimChannel(uint32_t c) { return c >> CtrDimShift; }
// Eight logical columns are exactly five physical columns. Keep sprite sampling
// in phase between the two eyes instead of changing alternate column widths.
constexpr int CtrStereoOffsetFor(float slider) { return slider <= 0.01f ? 0 : slider < 0.6f ? -8 : -16; }
} // namespace devilution
