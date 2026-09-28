#include "platform/ctr/ui_background.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "engine/assets.hpp"
#include "engine/palette.h"
#include "engine/render/primitive_render.hpp"
#include "platform/ctr/ui_geometry.hpp"

namespace devilution {
namespace {

constexpr int BackgroundWidth = 400;
constexpr int BackgroundHeight = 240;
constexpr size_t BackgroundBytes = BackgroundWidth * BackgroundHeight;

constexpr size_t BackgroundCount = static_cast<size_t>(CtrPanelBackground::Count);
std::optional<OwnedSurface> Backgrounds[BackgroundCount];
bool BackgroundLoadAttempted[BackgroundCount] = {};
std::optional<OwnedSurface> BottomBackground;
bool BottomBackgroundLoadAttempted = false;
constexpr int VerticalBarWidth = 26;
constexpr int VerticalBarHeight = 84;
constexpr int ExperienceBarWidth = 224;
constexpr int ExperienceBarHeight = 14;
constexpr size_t BarCount = 3;
std::optional<OwnedSurface> Bars[BarCount];
bool BarLoadAttempted[BarCount] = {};

void LoadBar(size_t index)
{
	BarLoadAttempted[index] = true;
	constexpr std::array<const char *, BarCount> Paths {
	    "data\\ctr_health_bar.pal8",
	    "data\\ctr_mana_bar.pal8",
	    "data\\ctr_experience_bar.pal8",
	};
	constexpr std::array<int, BarCount> Widths { VerticalBarWidth, VerticalBarWidth, ExperienceBarWidth };
	constexpr std::array<int, BarCount> Heights { VerticalBarHeight, VerticalBarHeight, ExperienceBarHeight };
	auto image = LoadAsset(Paths[index]);
	if (!image || image->size != static_cast<size_t>(Widths[index] * Heights[index]))
		return;
	Bars[index].emplace(Widths[index], Heights[index]);
	const auto *src = reinterpret_cast<const uint8_t *>(image->data.get());
	for (int y = 0; y < Heights[index]; ++y)
		std::memcpy(Bars[index]->at(0, y), src + y * Widths[index], Widths[index]);
}

void LoadBackground(size_t index)
{
	BackgroundLoadAttempted[index] = true;
	constexpr std::array<const char *, BackgroundCount> Paths {
	    "data\\ctr_character_background.pal8",
	    "data\\ctr_quest_background.pal8",
	    "data\\ctr_spells_background.pal8",
	    "data\\ctr_inventory_warrior.pal8",
	    "data\\ctr_inventory_rogue.pal8",
	    "data\\ctr_inventory_sorcerer.pal8",
	    "data\\ctr_inventory_warrior_stash.pal8",
	    "data\\ctr_inventory_rogue_stash.pal8",
	    "data\\ctr_inventory_sorcerer_stash.pal8",
	};
	auto image = LoadAsset(Paths[index]);
	if (!image || image->size != BackgroundBytes)
		return;

	Backgrounds[index].emplace(BackgroundWidth, BackgroundHeight);
	const auto *src = reinterpret_cast<const uint8_t *>(image->data.get());
	for (int y = 0; y < BackgroundHeight; ++y) {
		std::memcpy(Backgrounds[index]->at(0, y), src + y * BackgroundWidth, BackgroundWidth);
	}
}

void LoadBottomBackground()
{
	BottomBackgroundLoadAttempted = true;
	auto image = LoadAsset("data\\ctr_bottom_ui.pal8");
	if (!image || image->size != 320 * 240)
		return;
	BottomBackground.emplace(320, 240);
	const auto *src = reinterpret_cast<const uint8_t *>(image->data.get());
	for (int y = 0; y < 240; ++y)
		std::memcpy(BottomBackground->at(0, y), src + y * 320, 320);
}

} // namespace

void DrawCtrPanelBackground(const Surface &out, CtrPanelBackground background)
{
	const size_t index = static_cast<size_t>(background);
	if (!BackgroundLoadAttempted[index])
		LoadBackground(index);
	if (!Backgrounds[index]) {
		FillRect(out, 0, 0, out.w(), out.h(), 0);
		UnsafeDrawBorder2px(out, { { 1, 1 }, { out.w() - 2, out.h() - 2 } }, PAL16_BEIGE + 8);
		DrawHorizontalLine(out, { 3, 3 }, out.w() - 6, PAL16_BEIGE + 3);
		DrawVerticalLine(out, { 3, 3 }, out.h() - 6, PAL16_BEIGE + 3);
		return;
	}
	out.BlitFrom(*Backgrounds[index], { 0, 0, BackgroundWidth, BackgroundHeight }, { 0, 0 });
}

void DrawCtrBottomBackground(const Surface &out)
{
	if (!BottomBackgroundLoadAttempted)
		LoadBottomBackground();
	if (BottomBackground)
		out.BlitFromSkipColorIndexZero(*BottomBackground, { 0, 0, 320, 240 }, { 0, 0 });
}

void DrawCtrBarArtwork(const Surface &out, CtrBarArtwork bar, Point position, float fill)
{
	const size_t index = static_cast<size_t>(bar);
	if (!BarLoadAttempted[index])
		LoadBar(index);
	if (!Bars[index])
		return;
	const int width = bar == CtrBarArtwork::Experience ? ExperienceBarWidth : VerticalBarWidth;
	const int height = bar == CtrBarArtwork::Experience ? ExperienceBarHeight : VerticalBarHeight;
	const bool vertical = bar != CtrBarArtwork::Experience;
	const int fillPixels = static_cast<int>(std::lround(std::clamp(fill, 0.0f, 1.0f) * (vertical ? height : width)));
	if (fillPixels == 0)
		return;
	for (int y = 0; y < (vertical ? fillPixels : height); ++y) {
		for (int x = 0; x < (vertical ? width : fillPixels); ++x) {
			// Scale the whole motif into the filled region so its jeweled cap
			// follows the liquid edge as health/mana/experience changes.
			const int sourceX = vertical ? x : x * width / fillPixels;
			const int sourceY = vertical ? y * height / fillPixels : y;
			const uint8_t color = (*Bars[index])[Point { sourceX, sourceY }];
			if (color != 0)
				out.SetPixel({ position.x + x, position.y + (vertical ? height - fillPixels + y : y) }, color);
		}
	}
}

} // namespace devilution
