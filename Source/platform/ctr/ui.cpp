#include "platform/ctr/ui_background.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "control/control.hpp"
#include "engine/assets.hpp"
#include "engine/palette.h"
#include "engine/render/primitive_render.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "player.h"
#include "platform/ctr/ui_geometry.hpp"
#include "tables/playerdat.hpp"
#include "utils/display.h"

namespace devilution {
namespace {

constexpr int BackgroundWidth = 400;
constexpr int BackgroundHeight = 240;
constexpr size_t BackgroundBytes = BackgroundWidth * BackgroundHeight;

constexpr size_t BackgroundCount = static_cast<size_t>(CtrPanelBackground::Count);
std::optional<OwnedSurface> Backgrounds[BackgroundCount];
bool BackgroundLoadAttempted[BackgroundCount] = {};
std::optional<OwnedSurface> BottomBackground;
std::optional<OwnedSurface> BottomHudBuffer;
bool BottomBackgroundLoadAttempted = false;

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

void DrawLiquidBar(const Surface &out, Rectangle rect, int filled, uint8_t color, bool vertical)
{
	filled = std::clamp(filled, 0, vertical ? rect.size.height : rect.size.width);
	const int phase = SDL_GetTicks() / 90;
	for (int y = 0; y < rect.size.height; ++y) {
		for (int x = 0; x < rect.size.width; ++x) {
			if (vertical ? y < rect.size.height - filled : x >= filled)
				continue;
			const int shimmer = (x * 3 + y + phase) % 7;
			const int highlight = vertical && y == rect.size.height - filled ? 3 : 0;
			*out.at(rect.position.x + x, rect.position.y + y) = color + 5 + shimmer + highlight;
		}
	}
}

int CtrExperienceFill()
{
	const Player &player = *MyPlayer;
	if (player.isMaxCharacterLevel())
		return CtrBottomExperienceBar.size.width;
	const int level = player.getCharacterLevel();
	const uint64_t previous = GetNextExperienceThresholdForLevel(level - 1);
	const uint64_t next = GetNextExperienceThresholdForLevel(level);
	if (next <= previous || player._pExperience <= previous)
		return 0;
	return static_cast<int>(std::min<uint64_t>(CtrBottomExperienceBar.size.width,
	    (player._pExperience - previous) * CtrBottomExperienceBar.size.width / (next - previous)));
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

void DrawCtrBottomHud(const Surface &out)
{
	if (!BottomBackgroundLoadAttempted)
		LoadBottomBackground();
	if (!BottomHudBuffer)
		BottomHudBuffer.emplace(320, 240);
	Surface &hud = *BottomHudBuffer;
	FillRect(hud, 0, 0, 320, 240, 0);
	DrawLiquidBar(hud, CtrBottomHealthBar, CtrBottomHealthBar.size.height * std::clamp(MyPlayer->_pHPPer, 0, 81) / 81, PAL16_RED, true);
	DrawLiquidBar(hud, CtrBottomManaBar, CtrBottomManaBar.size.height * std::clamp(MyPlayer->_pManaPer, 0, 81) / 81, PAL16_BLUE, true);
	DrawLiquidBar(hud, CtrBottomExperienceBar, CtrExperienceFill(), PAL16_YELLOW, false);
	if (BottomBackground)
		hud.BlitFromSkipColorIndexZero(*BottomBackground, { 0, 0, 320, 240 }, { 0, 0 });
	DrawCtrInvBelt(hud);
	DrawInfoBox(hud);
	out.ScaleBlitFrom(hud, { 0, 0, 320, 240 }, { 0, 240, gnScreenWidth, 240 });
}

} // namespace devilution
