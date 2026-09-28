#pragma once

#include "engine/rectangle.hpp"

namespace devilution {

// SDL_DUALSCR uses a 640x480 logical canvas. The upper half is displayed
// at 400x240 and the lower half at 320x240. Panel text uses native top pixels.
constexpr Size CtrTopSize { 400, 240 };
constexpr Size CtrLegacyPanelSize { 320, 352 };
constexpr int CtrItemSlotPixels = 16;
constexpr int CtrItemSlotPitch = 17;
constexpr Rectangle CtrBottomHealthBar { { 20, 82 }, { 26, 84 } };
constexpr Rectangle CtrBottomManaBar { { 274, 82 }, { 26, 84 } };
constexpr Rectangle CtrBottomExperienceBar { { 48, 206 }, { 224, 14 } };
constexpr Rectangle CtrBottomInfoBox { { 64, 75 }, { 191, 118 } };

constexpr Rectangle CtrBottomBeltSlot(int slot)
{
	return { { 69 + slot * 23 + (slot >= 4 ? 1 : 0), 36 }, { 21, 21 } };
}
constexpr Rectangle CtrStatButtons[4] = {
	{ { 179, 108 }, { 22, 20 } },
	{ { 179, 132 }, { 22, 20 } },
	{ { 179, 156 }, { 22, 20 } },
	{ { 179, 180 }, { 22, 20 } },
};
constexpr int CtrSpellRowsY = 46;
constexpr int CtrSpellRowHeight = 24;
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

constexpr Point CtrTopToScreen(Point p, int screenWidth)
{
	return { (p.x * screenWidth + CtrTopSize.width / 2) / CtrTopSize.width, p.y };
}

constexpr Point CtrScreenToTop(Point p, int screenWidth)
{
	if (p.x < 0 || p.x >= screenWidth || p.y < 0 || p.y >= CtrTopSize.height)
		return { -1, -1 };
	return { p.x * CtrTopSize.width / screenWidth, p.y };
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
