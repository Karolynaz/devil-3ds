#pragma once

#include "engine/palette.h"
#include "engine/render/primitive_render.hpp"
#include "platform/ctr/ui_background.hpp"
#include "platform/ctr/ui_geometry.hpp"
#include "qol/stash.h"
#include "qol/visual_store.h"
#include "utils/display.h"

namespace devilution {

inline Point CtrTopToScreen(Point p) { return CtrTopToScreen(p, gnScreenWidth); }
inline Point CtrScreenToTop(Point p) { return CtrScreenToTop(p, gnScreenWidth); }
inline Point CtrInventoryToScreen(Point p) { return CtrInventoryToScreen(p, gnScreenWidth, IsStashOpen || IsVisualStoreOpen); }
inline Point CtrScreenToInventory(Point p) { return CtrScreenToInventory(p, gnScreenWidth, IsStashOpen || IsVisualStoreOpen); }
inline Rectangle CtrInventoryScreenRect() { return CtrInventoryScreenRect(gnScreenWidth, IsStashOpen || IsVisualStoreOpen); }

inline void DrawCtrPanelFrame(const Surface &out)
{
	FillRect(out, 0, 0, out.w(), out.h(), 0);
	UnsafeDrawBorder2px(out, { { 1, 1 }, { out.w() - 2, out.h() - 2 } }, PAL16_BEIGE + 8);
	DrawHorizontalLine(out, { 3, 3 }, out.w() - 6, PAL16_BEIGE + 3);
	DrawVerticalLine(out, { 3, 3 }, out.h() - 6, PAL16_BEIGE + 3);
}

void DrawCtrBottomBackground(const Surface &out);
void DrawCtrBottomHud(const Surface &out);

} // namespace devilution
