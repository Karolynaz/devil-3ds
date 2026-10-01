#pragma once

/**
 * @file world_view.hpp
 *
 * @brief The 3DS game world buffer, its zoom levels and the presenter's sampling tables.
 *
 * The world is drawn into its own 8-bit buffer of (400 / s) x (240 / s) pixels,
 * where s is the "World zoom" scale. The top-screen presenter shows it on the
 * 400x240 screen, in every pixel where the UI layer still has the key index.
 *
 * This part of the header has no dependency on SDL or the 3DS libraries, so a
 * host program can test the tables (tools/check_world_zoom.cpp).
 */

#include <cstdint>

namespace devilution {

/** @brief Values of the "World zoom" option. The value is the index in CtrZoomScales. */
enum class CtrWorldZoom : uint8_t {
	Percent100,
	Percent80,
	Percent67,
	Percent62_5,
};

constexpr int CtrZoomLevelCount = 4;

/** @brief The scale s = num / den of a zoom level. */
struct CtrZoomScale {
	uint8_t num;
	uint8_t den;
};

constexpr CtrZoomScale CtrZoomScales[CtrZoomLevelCount] = {
	{ 1, 1 }, // 100%
	{ 4, 5 }, // 80%
	{ 2, 3 }, // 66.7%
	{ 5, 8 }, // 62.5%
};

constexpr int CtrScreenWidth = 400;
constexpr int CtrScreenHeight = 240;

/** @brief World width (400 / s) for a zoom level index. */
constexpr int CtrWorldWidthFor(int level) { return CtrScreenWidth * CtrZoomScales[level].den / CtrZoomScales[level].num; }
/** @brief World height (240 / s) for a zoom level index. */
constexpr int CtrWorldHeightFor(int level) { return CtrScreenHeight * CtrZoomScales[level].den / CtrZoomScales[level].num; }

static_assert(CtrWorldWidthFor(0) == 400 && CtrWorldHeightFor(0) == 240);
static_assert(CtrWorldWidthFor(1) == 500 && CtrWorldHeightFor(1) == 300);
static_assert(CtrWorldWidthFor(2) == 600 && CtrWorldHeightFor(2) == 360);
static_assert(CtrWorldWidthFor(3) == 640 && CtrWorldHeightFor(3) == 384);
// Every level divides exactly, so the sizes are integers.
static_assert(CtrScreenWidth * CtrZoomScales[1].den % CtrZoomScales[1].num == 0);
static_assert(CtrScreenWidth * CtrZoomScales[2].den % CtrZoomScales[2].num == 0);
static_assert(CtrScreenWidth * CtrZoomScales[3].den % CtrZoomScales[3].num == 0);

/**
 * @brief Palette index of the "world" key in the UI layer.
 *
 * Before the UI is drawn, the top area of the main surface is cleared to this
 * index. A pixel that still has it after the UI is drawn shows the world.
 * See tasks/021-world-zoom.NOTES.md for why no UI art, text or cursor can make
 * this index. The blend table never returns it on 3DS (palette.cpp).
 */
constexpr uint8_t CtrWorldKeyIndex = 2;

/**
 * @brief Palette index of the "dim world" key in the UI layer.
 *
 * A half-transparent UI draw that meets a key pixel (CtrWorldKeyIndex) writes
 * this index. The presenter shows the world there, darkened by CtrDimShift
 * (the same as the original blend with black). The UI art, fonts and cursors
 * never make indexes 1..46 (tasks/021-world-zoom.NOTES.md); the blend table
 * never returns it on 3DS (palette.cpp).
 */
constexpr uint8_t CtrWorldDimKeyIndex = 1;

/**
 * @brief The dim key darkens the world color by 1 / 2: `(c + 0) >> 1` per channel.
 *
 * Source: the blend of a half-transparent box with black is
 * `BlendColors(black, c) = (0 + c) / 2` per channel (utils/palette_blending.cpp),
 * then the nearest palette color. The presenter uses the exact half, without
 * the nearest-color step.
 */
constexpr int CtrDimShift = 1;

/** @brief Darkens a packed red/blue lane pair (`r | b << 16`) by CtrDimShift. */
constexpr uint32_t CtrDimRedBlue(uint32_t redBlue)
{
	return (redBlue >> CtrDimShift) & 0x00FF00FFu;
}

/** @brief Darkens one 8 bit channel by CtrDimShift. */
constexpr uint32_t CtrDimChannel(uint32_t c)
{
	return c >> CtrDimShift;
}

/** @brief Fixed-point one for the interpolation weights (8 bit fraction). */
constexpr int CtrWeightOne = 256;

/**
 * @brief Interpolation data for one output pixel on one axis.
 *
 * The sample is `src[i0] * (CtrWeightOne - w) + src[i1] * w`, in 8.8 fixed point.
 */
struct CtrAxisSample {
	uint16_t i0;
	uint16_t i1;
	uint16_t w;
};

/**
 * @brief Fills the tables for one axis (outSize output pixels from srcSize world pixels).
 *
 * Output pixel i has its center at (i + 0.5) / outSize of the axis. The world
 * sample position is that center in world pixels, minus 0.5 (the center of
 * world pixel 0), so u = ((2i + 1) * srcSize - outSize) / (2 * outSize).
 * The positions outside the world are clamped to the edge pixel.
 * Division is used here only; this runs when the level changes.
 * When srcSize == outSize every entry is { i, i, 0 }: exact 1:1.
 */
inline void CtrBuildAxisSamples(CtrAxisSample *table, int outSize, int srcSize)
{
	const int last = srcSize - 1;
	const int denominator = 2 * outSize;
	for (int i = 0; i < outSize; ++i) {
		const int n = (2 * i + 1) * srcSize - outSize;
		int i0 = 0;
		int w = 0;
		if (n > 0) {
			i0 = n / denominator;
			w = ((n % denominator) * CtrWeightOne + outSize) / denominator;
			if (w >= CtrWeightOne) {
				w = 0;
				++i0;
			}
		}
		if (i0 >= last) {
			i0 = last;
			w = 0;
		}
		table[i].i0 = static_cast<uint16_t>(i0);
		table[i].i1 = static_cast<uint16_t>(w == 0 ? i0 : i0 + 1);
		table[i].w = static_cast<uint16_t>(w);
	}
}

/** @brief The sampling tables of one zoom level. */
struct CtrWorldSampling {
	int level;
	int worldWidth;
	int worldHeight;
	CtrAxisSample columns[CtrScreenWidth];
	CtrAxisSample rows[CtrScreenHeight];
};

inline void CtrBuildWorldSampling(CtrWorldSampling &sampling, int level)
{
	sampling.level = level;
	sampling.worldWidth = CtrWorldWidthFor(level);
	sampling.worldHeight = CtrWorldHeightFor(level);
	CtrBuildAxisSamples(sampling.columns, CtrScreenWidth, sampling.worldWidth);
	CtrBuildAxisSamples(sampling.rows, CtrScreenHeight, sampling.worldHeight);
}

/**
 * @brief Converts a 640 wide UI position to world pixels.
 *
 * x_world = x * 400 / 640 / s and y_world = y / s, with s = num / den.
 */
constexpr int CtrUiToWorldX(int x, int uiWidth, int level)
{
	return x * CtrScreenWidth * CtrZoomScales[level].den / (uiWidth * CtrZoomScales[level].num);
}
constexpr int CtrUiToWorldY(int y, int level)
{
	return y * CtrZoomScales[level].den / CtrZoomScales[level].num;
}

/** @brief Values of the "Zoom control" option: who sets the world zoom. */
enum class CtrZoomControl : uint8_t {
	Menu,
	Slider3D,
};

/** @brief Distance past a band edge that the 3D slider must go before the zoom level changes. */
constexpr float CtrSliderHysteresis = 0.04f;

/**
 * @brief Maps the 3D slider (0.0 bottom, 1.0 top) to a zoom level, with hysteresis.
 *
 * The 4 bands have the edges 0.25, 0.5 and 0.75: bottom = level 0 (100%), top = level 3 (62.5%).
 * The level changes only when the value is more than CtrSliderHysteresis past an edge of the
 * current band, so a value that rests on an edge does not flicker.
 */
constexpr int CtrSliderZoomLevel(float slider, int current)
{
	if (current < 0)
		current = 0;
	if (current > CtrZoomLevelCount - 1)
		current = CtrZoomLevelCount - 1;
	while (current < CtrZoomLevelCount - 1 && slider > static_cast<float>(current + 1) / CtrZoomLevelCount + CtrSliderHysteresis)
		++current;
	while (current > 0 && slider < static_cast<float>(current) / CtrZoomLevelCount - CtrSliderHysteresis)
		--current;
	return current;
}

/** @brief The current "World zoom" level (index in CtrZoomScales). Reads the option; defined in utils/display.cpp. */
int CtrWorldZoomLevel();
/** @brief Width of the world view at the current zoom level. */
inline int CtrWorldWidth() { return CtrWorldWidthFor(CtrWorldZoomLevel()); }
/** @brief Height of the world view at the current zoom level. */
inline int CtrWorldHeight() { return CtrWorldHeightFor(CtrWorldZoomLevel()); }

} // namespace devilution
