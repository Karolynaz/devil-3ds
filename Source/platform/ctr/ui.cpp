#include "platform/ctr/ui_background.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "engine/assets.hpp"
#include "engine/palette.h"
#include "engine/render/primitive_render.hpp"

namespace devilution {
namespace {

constexpr int BackgroundWidth = 400;
constexpr int BackgroundHeight = 240;
constexpr size_t BackgroundBytes = BackgroundWidth * BackgroundHeight;

constexpr size_t BackgroundCount = static_cast<size_t>(CtrPanelBackground::Count);
std::optional<OwnedSurface> Backgrounds[BackgroundCount];
bool BackgroundLoadAttempted[BackgroundCount] = {};

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

} // namespace devilution
