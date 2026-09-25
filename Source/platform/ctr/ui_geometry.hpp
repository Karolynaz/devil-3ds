#pragma once

#include "engine/rectangle.hpp"

namespace devilution {

// SDL_DUALSCR uses a 640x480 logical canvas. The upper half is displayed
// at 400x240 and the lower half at 320x240. Panel text uses native top pixels.
constexpr Size CtrTopSize { 400, 240 };
constexpr Size CtrLegacyPanelSize { 320, 352 };
constexpr Rectangle CtrInventoryContent { { 91, 0 }, { 218, 240 } };
constexpr Rectangle CtrStatButtons[4] = {
	{ { 179, 108 }, { 22, 20 } },
	{ { 179, 132 }, { 22, 20 } },
	{ { 179, 156 }, { 22, 20 } },
	{ { 179, 180 }, { 22, 20 } },
};
constexpr int CtrSpellRowsY = 27;
constexpr int CtrSpellRowHeight = 27;
constexpr int CtrSpellTabsY = 218;

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

constexpr Rectangle CtrInventoryScreenRect(int screenWidth, bool split)
{
	if (split)
		return { { screenWidth - 218, 0 }, { 218, 240 } };
	const int width = CtrInventoryContent.size.width * screenWidth / CtrTopSize.width;
	return { { (screenWidth - width) / 2, 0 }, { width, 240 } };
}

constexpr Point CtrInventoryToScreen(Point p, int screenWidth, bool split)
{
	const Rectangle rect = CtrInventoryScreenRect(screenWidth, split);
	return { rect.position.x + (p.x * rect.size.width + 160) / 320,
		(p.y * rect.size.height + 176) / 352 };
}

constexpr Point CtrScreenToInventory(Point p, int screenWidth, bool split)
{
	const Rectangle rect = CtrInventoryScreenRect(screenWidth, split);
	if (!rect.contains(p))
		return { -1, -1 }; // Never interpret bottom-screen or margin pixels as inventory slots.
	return { (p.x - rect.position.x) * 320 / rect.size.width, p.y * 352 / rect.size.height };
}

} // namespace devilution
