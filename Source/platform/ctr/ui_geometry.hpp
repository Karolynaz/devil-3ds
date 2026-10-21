#pragma once

#include "engine/rectangle.hpp"
#include "platform/ctr/world_view.hpp"

namespace devilution {

// SDL_DUALSCR uses a 640x480 logical canvas. The upper half is displayed
// at 400x240 and the lower half at 320x240. Panel text uses native top pixels.
constexpr Size CtrTopSize { 400, 240 };
constexpr Size CtrLegacyPanelSize { 320, 352 };
constexpr int CtrItemSlotPixels = 16;
constexpr int CtrItemSlotPitch = 17;
constexpr Rectangle CtrBottomHealthBar { { 20, 68 }, { 25, 93 } };
constexpr Rectangle CtrBottomManaBar { { 275, 68 }, { 25, 93 } };
constexpr Rectangle CtrBottomExperienceBar { { 57, 169 }, { 206, 2 } };
constexpr Rectangle CtrBottomInfoBox { { 64, 51 }, { 192, 113 } };

constexpr Rectangle CtrBottomBeltSlot(int slot)
{
	return { { 63 + slot * 24 + (slot >= 4 ? 4 : 0), 10 }, { 22, 22 } };
}
constexpr Rectangle CtrStatButtons[4] = {
	{ { 179, 108 }, { 22, 20 } },
	{ { 179, 132 }, { 22, 20 } },
	{ { 179, 156 }, { 22, 20 } },
	{ { 179, 180 }, { 22, 20 } },
};
// Spell binding slots share one geometry for rendering, hit tests and focus.
constexpr Rectangle CtrHudSpellSlot(int slot)
{
	return { { 62 + slot * 50, 176 }, { 45, 45 } };
}
// Render bounds include Figma's 4px badge shadow. Half-pixel X rounds right.
constexpr Point CtrHudBadgePositions[] = { { 73, 217 }, { 122, 217 }, { 174, 217 }, { 225, 217 } };
constexpr int CtrHudBadgeSize = 23;

constexpr int CtrSpellShortcutCount = 4;
constexpr Point CtrSpellBindingPositions[] = {
	{ 61, 100 }, { 141, 100 }, { 221, 100 }, { 301, 100 }
};
constexpr Rectangle CtrSpellBindingRect(int slot)
{
	return { CtrSpellBindingPositions[slot], { 37, 38 } };
}
inline int CtrSpellBindingHit(Point point)
{
	for (int slot = 0; slot < CtrSpellShortcutCount; ++slot)
		if (CtrSpellBindingRect(slot).contains(point))
			return slot;
	return -1;
}

constexpr int CtrSpellRowsY = 46;
constexpr int CtrSpellRowHeight = 40;
constexpr int CtrSpellRowsPerPage = 4;
constexpr int CtrSpellTabsY = 214;
constexpr int CtrSpellTabFirstX = 16;
constexpr int CtrSpellTabWidth = 85;
constexpr int CtrSpellTabHeight = 17;
constexpr int CtrSpellTabGap = 10;

constexpr Rectangle CtrSpellTabRect(int page, bool hellfire)
{
	// Diablo has four framed tabs in the supplied artwork. Hellfire has a
	// fifth page, so it uses five smaller targets within the same strip.
	return hellfire
	    ? Rectangle { { CtrSpellTabFirstX + page * 74, CtrSpellTabsY }, { 66, CtrSpellTabHeight } }
	    : Rectangle { { CtrSpellTabFirstX + page * (CtrSpellTabWidth + CtrSpellTabGap), CtrSpellTabsY }, { CtrSpellTabWidth, CtrSpellTabHeight } };
}

/**
 * @brief Surface column that the 3DS presenter reads for output column x.
 *
 * CTR_PresentFrame reads UI column `x * 640 / outputWidth` for each of the
 * outputWidth screen columns (the UI layer is 640 wide and is squeezed).
 */
constexpr int CtrPresenterSourceColumn(int x, int outputWidth)
{
	return x * 640 / outputWidth;
}

static_assert(CtrPresenterSourceColumn(0, CtrTopSize.width) == 0);
static_assert(CtrPresenterSourceColumn(CtrTopSize.width - 1, CtrTopSize.width) == 638);
static_assert(CtrPresenterSourceColumn(399, 400) < 640);

/**
 * @brief Converts a position in the 640 wide mouse space to world pixels.
 *
 * World coordinates use the native 400x240 camera. Unlike CtrScreenToTop this does not reject
 * positions, because world hit tests run for any cursor position.
 */
constexpr Point CtrScreenToWorld(Point p, int screenWidth)
{
	return { CtrUiToWorldX(p.x, screenWidth), p.y };
}

constexpr Point CtrTopToScreen(Point p, int screenWidth)
{
	return { p.x * screenWidth / CtrTopSize.width, p.y };
}

constexpr Point CtrScreenToTop(Point p, int screenWidth)
{
	if (p.x < 0 || p.x >= screenWidth || p.y < 0 || p.y >= CtrTopSize.height)
		return { -1, -1 };
	return { CtrUiToWorldX(p.x, screenWidth), p.y };
}

constexpr Rectangle CtrInventorySlotRect(int slot, bool split)
{
	const int shift = split ? 97 : 0;
	switch (slot) {
	case 0: return { { 183 + shift, 20 }, { 33, 33 } }; // helmet
	case 1: return { { 132 + shift, 124 }, { 16, 16 } }; // left ring
	case 2: return { { 251 + shift, 125 }, { 16, 16 } }; // right ring
	case 3: return { { 234 + shift, 37 }, { 16, 16 } }; // amulet
	case 4: return { { 116 + shift, 64 }, { 33, 50 } }; // left hand
	case 5: return { { 250 + shift, 64 }, { 33, 50 } }; // right hand
	case 6: return { { 183 + shift, 64 }, { 33, 50 } }; // armor
	default:
		if (slot < 7 || slot > 46)
			return {};
		return { { 115 + shift + ((slot - 7) % 10) * CtrItemSlotPitch,
		             152 + ((slot - 7) / 10) * CtrItemSlotPitch },
			{ CtrItemSlotPixels, CtrItemSlotPixels } };
	}
}

constexpr Rectangle CtrStashSlotRect(Point slot)
{
	return { { 19 + slot.x * CtrItemSlotPitch, 50 + slot.y * CtrItemSlotPitch },
		{ CtrItemSlotPixels, CtrItemSlotPixels } };
}

Rectangle CtrInventoryScreenRect(int screenWidth, bool split);
Point CtrInventoryToScreen(Point p, int screenWidth, bool split);
Point CtrScreenToInventory(Point p, int screenWidth, bool split);

} // namespace devilution
