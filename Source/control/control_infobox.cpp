#include "control.hpp"
#include "control_panel.hpp"
#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif
#ifdef __3DS__
#include "platform/ctr/ui_geometry.hpp"
#endif
#include "controls/control_mode.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "inv.h"
#include "levels/trigs.h"
#include "options.h"
#include "panels/partypanel.hpp"
#include "qol/stash.h"
#include "qol/visual_store.h"
#include "qol/xpbar.h"
#include "towners.h"
#include "utils/algorithm/container.hpp"
#include "utils/format.hpp"
#include "utils/format_int.hpp"
#include "utils/log.hpp"
#include "utils/screen_reader.hpp"
#include "utils/str_cat.hpp"
#include "utils/str_split.hpp"

namespace devilution {

StringOrView InfoString;
StringOrView FloatingInfoString;

#ifdef __3DS__
namespace {
uint64_t GameStartHintTime;
bool ShowGameStartHint;
} // namespace

void Reset3DSGameStartHint()
{
	GameStartHintTime = SDL_GetTicks();
	ShowGameStartHint = true;
}
#endif

namespace {

void PrintInfo(const Surface &out)
{
	if (ChatFlag || InfoString.empty())
		return;

#ifdef __3DS__
	Rectangle infoBox = CtrBottomInfoBox;
#else
	Rectangle infoBox = InfoBoxRect;

	SetPanelObjectPosition(UiPanels::Main, infoBox);
#endif

	SpeakText(InfoString);

	const int boxWidth = infoBox.size.width;
	const int boxHeight = infoBox.size.height;
	const int horizontalPadding = 4;
	const int verticalPadding = 4;
	const int maxLineWidth = std::max(20, boxWidth - (horizontalPadding * 2));
	const int availableHeight = std::max(20, boxHeight - (verticalPadding * 2));

	static std::string LastInfoText;
	static uint32_t InfoScrollStartTime = 0;

	const uint32_t currentTicks = SDL_GetTicks();
	if (InfoString.str() != LastInfoText) {
		LastInfoText = std::string(InfoString.str());
		InfoScrollStartTime = currentTicks;
	}

	const std::string wrappedText = WordWrapString(InfoString.str(), maxLineWidth, GameFont12, 1);
	std::vector<std::string> lines;
	for (auto lineView : SplitByChar(wrappedText, '\n')) {
		lines.emplace_back(lineView);
	}
	if (lines.empty())
		return;

	const int numLines = static_cast<int>(lines.size());
	int lineGap = 4;
	if (numLines <= 2)
		lineGap = 8;
	else if (numLines <= 4)
		lineGap = 5;
	else if (numLines <= 6)
		lineGap = 3;
	else
		lineGap = 2;

	const int fontHeight = 12;
	const int lineHeight = fontHeight + lineGap;
	const int totalTextHeight = numLines * fontHeight + (numLines - 1) * lineGap;

	int baseY = verticalPadding;
	if (totalTextHeight <= availableHeight) {
		baseY = (boxHeight - totalTextHeight) / 2;
	} else {
		const int overflowY = totalTextHeight - availableHeight;
		const uint32_t pauseTop = 1500;
		const uint32_t scrollSpeed = 22; // pixels per second
		const uint32_t scrollDuration = std::max<uint32_t>(1000, (overflowY * 1000) / scrollSpeed);
		const uint32_t pauseBottom = 1800;
		const uint32_t fadeGap = 500;
		const uint32_t cycleDuration = pauseTop + scrollDuration + pauseBottom + fadeGap;

		const uint32_t elapsed = currentTicks - InfoScrollStartTime;
		const uint32_t t = elapsed % cycleDuration;

		int scrollY = 0;
		if (t < pauseTop) {
			scrollY = 0;
		} else if (t < pauseTop + scrollDuration) {
			const float progress = static_cast<float>(t - pauseTop) / static_cast<float>(scrollDuration);
			scrollY = static_cast<int>(progress * overflowY);
		} else {
			scrollY = overflowY;
		}
		baseY = verticalPadding - scrollY;
	}

	const Surface boxSurface = out.subregion(infoBox.position.x, infoBox.position.y, boxWidth, boxHeight);

	for (int i = 0; i < numLines; ++i) {
		const std::string &line = lines[i];
		const int lineY = baseY + i * lineHeight;
		if (lineY + fontHeight <= 0 || lineY >= boxHeight)
			continue;

		const int lineWidth = GetLineWidth(line, GameFont12, 1);
		int lineX = horizontalPadding;
		if (lineWidth <= maxLineWidth) {
			lineX = (boxWidth - lineWidth) / 2;
		} else {
			const int overflowX = lineWidth - maxLineWidth;
			const uint32_t pauseStartX = 1200;
			const uint32_t scrollSpeedX = 26; // pixels per second
			const uint32_t scrollDurationX = std::max<uint32_t>(1000, (overflowX * 1000) / scrollSpeedX);
			const uint32_t pauseEndX = 1200;
			const uint32_t cycleDurationX = pauseStartX + scrollDurationX + pauseEndX + 400;

			const uint32_t elapsedX = currentTicks - InfoScrollStartTime;
			const uint32_t tX = elapsedX % cycleDurationX;

			int scrollX = 0;
			if (tX < pauseStartX) {
				scrollX = 0;
			} else if (tX < pauseStartX + scrollDurationX) {
				const float progressX = static_cast<float>(tX - pauseStartX) / static_cast<float>(scrollDurationX);
				scrollX = static_cast<int>(progressX * overflowX);
			} else {
				scrollX = overflowX;
			}
			lineX = horizontalPadding - scrollX;
		}

		const Rectangle lineRect { { lineX, lineY }, { std::max(boxWidth * 2, lineWidth + 20), lineHeight } };
		DrawString(boxSurface, line, lineRect, { .flags = InfoColor, .spacing = 1, .lineHeight = lineHeight });
	}
}

Rectangle GetFloatingInfoRect(std::string_view text, const int lineHeight, const int textSpacing)
{
	// Calculate the width and height of the floating info box
	auto lines = SplitByChar(text, '\n');
	const GameFontTables font = GameFont12;
	int maxW = 0;
	int lineCount = 0;

	for (const auto &line : lines) {
#ifdef __3DS__
		const bool belt = pcursinvitem >= INVITEM_BELT_FIRST && pcursinvitem < INVITEM_BELT_FIRST + MaxBeltItems;
		const int w = GetLineWidth(line, font, textSpacing, nullptr, belt ? CtrTextScale::BottomScreen : CtrTextScale::TopScreen);
#else
		const int w = GetLineWidth(line, font, textSpacing, nullptr);
#endif
		maxW = std::max(maxW, w);
		++lineCount;
	}

	const int totalH = std::max(1, lineCount) * lineHeight;

#ifdef __3DS__
	// Use the same native slots as the inventory/stash artwork, not the
	// original 320x352 desktop panel. Belt tooltips belong to the lower screen.
	Rectangle anchor {};
	const Player &player = *InspectPlayer;
	if (pcursinvitem >= INVITEM_HEAD && pcursinvitem < INVITEM_INV_FIRST) {
		anchor = CtrInventorySlotRect(pcursinvitem - INVITEM_HEAD, IsStashOpen);
	} else if (pcursinvitem >= INVITEM_INV_FIRST && pcursinvitem <= INVITEM_INV_LAST) {
		const int itemIdx = pcursinvitem - INVITEM_INV_FIRST;
		for (int j = 0; j < InventoryGridCells; ++j) {
			if (player.InvGrid[j] > 0 && player.InvGrid[j] - 1 == itemIdx) {
				anchor = CtrInventorySlotRect(j + INVITEM_INV_FIRST, IsStashOpen);
				break;
			}
		}
	} else if (pcursinvitem >= INVITEM_BELT_FIRST && pcursinvitem < INVITEM_BELT_FIRST + MaxBeltItems) {
		anchor = CtrBottomBeltSlot(pcursinvitem - INVITEM_BELT_FIRST);
		return { { (anchor.position.x + anchor.size.width / 2) * 2 - maxW / 2,
		             240 + anchor.position.y + anchor.size.height }, { maxW, totalH } };
	} else if (pcursstashitem != StashStruct::EmptyCell) {
		for (auto slot : StashGridRange) {
			if (Stash.GetItemIdAtPosition(slot) == pcursstashitem) {
				anchor = CtrStashSlotRect(slot);
				break;
			}
		}
	}
	if (anchor.size.width > 0) {
		Point position = CtrTopToScreen({ anchor.position.x + anchor.size.width / 2,
		    anchor.position.y + anchor.size.height }, GetScreenWidth());
		return { { position.x - maxW / 2, position.y }, { maxW, totalH } };
	}
	return { { MousePosition.x - maxW / 2, std::clamp(MousePosition.y, 0, 239) }, { maxW, totalH } };
#else
	const Player &player = *InspectPlayer;

	// 1) Equipment (Rect position)
	if (pcursinvitem >= INVITEM_HEAD && pcursinvitem < INVITEM_INV_FIRST) {
		const int slot = pcursinvitem - INVITEM_HEAD;
		static constexpr Point equipLocal[] = {
			{ 133, 59 },
			{ 48, 205 },
			{ 249, 205 },
			{ 205, 60 },
			{ 17, 160 },
			{ 248, 160 },
			{ 133, 160 },
		};

		Point itemPosition = equipLocal[slot];
		auto &item = player.InvBody[slot];
		const Size frame = GetInvItemSize(item._iCurs + CURSOR_FIRSTITEM);

		if (slot == INVLOC_HAND_LEFT) {
			itemPosition.x += frame.width == InventorySlotSizeInPixels.width
			    ? InventorySlotSizeInPixels.width
			    : 0;
			itemPosition.y += frame.height == 3 * InventorySlotSizeInPixels.height
			    ? 0
			    : -InventorySlotSizeInPixels.height;
		} else if (slot == INVLOC_HAND_RIGHT) {
			itemPosition.x += frame.width == InventorySlotSizeInPixels.width
			    ? (InventorySlotSizeInPixels.width - 1)
			    : 1;
			itemPosition.y += frame.height == 3 * InventorySlotSizeInPixels.height
			    ? 0
			    : -InventorySlotSizeInPixels.height;
		}

		itemPosition.y++;                  // Align position to bottom left of the item graphic
		itemPosition.x += frame.width / 2; // Align position to center of the item graphic
		itemPosition.x -= maxW / 2;        // Align position to the center of the floating item info box

		const Point screen = GetPanelPosition(UiPanels::Inventory, itemPosition);

		return { { screen.x, screen.y }, { maxW, totalH } };
	}

	// 2) Inventory grid (Rect position)
	if (pcursinvitem >= INVITEM_INV_FIRST && pcursinvitem < INVITEM_INV_FIRST + InventoryGridCells) {
		const int itemIdx = pcursinvitem - INVITEM_INV_FIRST;

		for (int j = 0; j < InventoryGridCells; ++j) {
			if (player.InvGrid[j] > 0 && player.InvGrid[j] - 1 == itemIdx) {
				const Item &it = player.InvList[itemIdx];
				Point itemPosition = InvRect[j + SLOTXY_INV_FIRST].position;

				itemPosition.x += GetInventorySize(it).width * InventorySlotSizeInPixels.width / 2; // Align position to center of the item graphic
				itemPosition.x -= maxW / 2;                                                         // Align position to the center of the floating item info box

				const Point screen = GetPanelPosition(UiPanels::Inventory, itemPosition);

				return { { screen.x, screen.y }, { maxW, totalH } };
			}
		}
	}

	// 3) Belt (Rect position)
	if (pcursinvitem >= INVITEM_BELT_FIRST && pcursinvitem < INVITEM_BELT_FIRST + MaxBeltItems) {
		const int itemIdx = pcursinvitem - INVITEM_BELT_FIRST;
		for (int i = 0; i < MaxBeltItems; ++i) {
			if (player.SpdList[i].isEmpty())
				continue;
			if (i != itemIdx)
				continue;

			const Item &item = player.SpdList[i];
			Point itemPosition = InvRect[i + SLOTXY_BELT_FIRST].position;

			itemPosition.x += GetInventorySize(item).width * InventorySlotSizeInPixels.width / 2; // Align position to center of the item graphic
			itemPosition.x -= maxW / 2;                                                           // Align position to the center of the floating item info box

			const Point screen = GetMainPanel().position + Displacement { itemPosition.x, itemPosition.y };

			return { { screen.x, screen.y }, { maxW, totalH } };
		}
	}

	// 4) Stash (Rect position)
	if (pcursstashitem != StashStruct::EmptyCell) {
		for (auto slot : StashGridRange) {
			auto itemId = Stash.GetItemIdAtPosition(slot);
			if (itemId == StashStruct::EmptyCell)
				continue;
			if (itemId != pcursstashitem)
				continue;

			const Item &item = Stash.stashList[itemId];
			Point itemPosition = GetStashSlotCoord(slot);
			const Size itemGridSize = GetInventorySize(item);

			itemPosition.y += itemGridSize.height * (InventorySlotSizeInPixels.height + 1) - 1; // Align position to bottom left of the item graphic
			itemPosition.x += itemGridSize.width * InventorySlotSizeInPixels.width / 2;         // Align position to center of the item graphic
			itemPosition.x -= maxW / 2;                                                         // Align position to the center of the floating item info box

			return { { itemPosition.x, itemPosition.y }, { maxW, totalH } };
		}
	}

	// 5) Visual Store (Rect position)
	if (pcursstoreitem != -1) {
		const VisualStorePage &page = VisualStore.pages[VisualStore.currentPage];
		std::span<Item> allItems = GetVisualStoreItems();
		for (const auto &vsItem : page.items) {
			if (vsItem.index != pcursstoreitem)
				continue;

			const Item &item = allItems[vsItem.index];
			Point itemPosition = GetVisualStoreSlotCoord(vsItem.position);
			const Size itemGridSize = GetInventorySize(item);

			itemPosition.y += itemGridSize.height * (VisualStoreGridHeight + 1) - 1; // Align position to bottom left of the item graphic
			itemPosition.x += itemGridSize.width * VisualStoreGridWidth / 2;         // Align position to center of the item graphic
			itemPosition.x -= maxW / 2;                                              // Align position to the center of the floating item info box

			return { { itemPosition.x, itemPosition.y }, { maxW, totalH } };
		}
	}
	if (pcursstorebtn != -1) {
		return { GetVisualBtnCoord(pcursstorebtn).position, { maxW, totalH } };
	}

	return { { 0, 0 }, { 0, 0 } };
#endif
}

int GetHoverSpriteHeight()
{
#ifdef __3DS__
	if (pcursinvitem >= INVITEM_BELT_FIRST && pcursinvitem < INVITEM_BELT_FIRST + MaxBeltItems)
		return 21;
	return CtrItemSlotPixels;
#else
	if (pcursinvitem >= INVITEM_HEAD && pcursinvitem < INVITEM_INV_FIRST) {
		auto &it = (*InspectPlayer).InvBody[pcursinvitem - INVITEM_HEAD];
		return GetInvItemSize(it._iCurs + CURSOR_FIRSTITEM).height + 1;
	}
	if (pcursinvitem >= INVITEM_INV_FIRST
	    && pcursinvitem < INVITEM_INV_FIRST + InventoryGridCells) {
		const int idx = pcursinvitem - INVITEM_INV_FIRST;
		auto &it = (*InspectPlayer).InvList[idx];
		return (GetInventorySize(it).height * (InventorySlotSizeInPixels.height + 1))
		    - InventorySlotSizeInPixels.height;
	}
	if (pcursinvitem >= INVITEM_BELT_FIRST
	    && pcursinvitem < INVITEM_BELT_FIRST + MaxBeltItems) {
		const int idx = pcursinvitem - INVITEM_BELT_FIRST;
		auto &it = (*InspectPlayer).SpdList[idx];
		return (GetInventorySize(it).height * (InventorySlotSizeInPixels.height + 1))
		    - InventorySlotSizeInPixels.height - 1;
	}
	if (pcursstashitem != StashStruct::EmptyCell) {
		auto &it = Stash.stashList[pcursstashitem];
		return GetInventorySize(it).height * (InventorySlotSizeInPixels.height + 1);
	}
	if (pcursstoreitem != -1) {
		std::span<Item> allItems = GetVisualStoreItems();
		auto &it = allItems[pcursstoreitem];
		return GetInventorySize(it).height * (INV_SLOT_SIZE_PX + 1);
	}
	return InventorySlotSizeInPixels.height;
#endif
}

int ClampAboveOrBelow(int anchorY, int spriteH, int boxH, int pad, int linePad)
{
	const int yAbove = anchorY - spriteH - boxH - pad;
	const int yBelow = anchorY + (linePad / 2) + pad;
	return (yAbove >= 0) ? yAbove : yBelow;
}

void PrintFloatingInfo(const Surface &out)
{
	if (ChatFlag)
		return;
	if (FloatingInfoString.empty())
		return;

	const int verticalSpacing = 3;
	const int lineHeight = 12 + verticalSpacing;
	const int textSpacing = 2;
	const int hPadding = 5;
	const int vPadding = 4;

#ifdef __3DS__
	const bool belt = pcursinvitem >= INVITEM_BELT_FIRST && pcursinvitem < INVITEM_BELT_FIRST + MaxBeltItems;
	const int screenTop = belt ? 240 : 0;
	const int screenBottom = screenTop + 240;
	const int maxFloatingWidth = GetScreenWidth() - 40;
	const std::string wrappedFloating = WordWrapString(FloatingInfoString.str(), maxFloatingWidth, GameFont12, textSpacing,
	    belt ? CtrTextScale::BottomScreen : CtrTextScale::TopScreen);
#else
	const int maxFloatingWidth = std::max(100, std::min(260, GetScreenWidth() - 20));
	const std::string wrappedFloating = WordWrapString(FloatingInfoString.str(), maxFloatingWidth, GameFont12, textSpacing);
#endif

	Rectangle floatingInfoBox = GetFloatingInfoRect(wrappedFloating, lineHeight, textSpacing);

	// Prevent the floating info box from going off-screen horizontally
	floatingInfoBox.position.x = std::clamp(floatingInfoBox.position.x, hPadding,
	    std::max(hPadding, GetScreenWidth() - (floatingInfoBox.size.width + hPadding)));

	const int spriteH = GetHoverSpriteHeight();
	const int anchorY = floatingInfoBox.position.y;

	// Prevent the floating info box from going off-screen vertically
	floatingInfoBox.position.y = ClampAboveOrBelow(anchorY, spriteH, floatingInfoBox.size.height, vPadding, verticalSpacing);

#ifdef __3DS__
	const int textHeight = floatingInfoBox.size.height;
	floatingInfoBox.size.height = std::min(textHeight, 240 - 2 * (vPadding + 1));
	floatingInfoBox.position.y = std::clamp(floatingInfoBox.position.y, screenTop + vPadding + 1,
	    screenBottom - floatingInfoBox.size.height - vPadding - 1);
	static std::string lastFloatingText;
	static uint32_t floatingScrollStart = 0;
	const uint32_t now = SDL_GetTicks();
	if (lastFloatingText != FloatingInfoString.str()) {
		lastFloatingText = std::string(FloatingInfoString.str());
		floatingScrollStart = now;
	}
	const int overflow = textHeight - floatingInfoBox.size.height;
	const uint32_t duration = std::max(1000, overflow * 1000 / 22);
	const uint32_t elapsed = (now - floatingScrollStart) % (1500 + duration + 2300);
	const int scroll = elapsed <= 1500 ? 0 : elapsed < 1500 + duration
	    ? static_cast<int>((static_cast<uint64_t>(elapsed - 1500) * overflow) / duration) : overflow;
#endif
	SpeakText(FloatingInfoString);

	for (int i = 0; i < 3; i++)
		DrawHalfTransparentRectTo(out, floatingInfoBox.position.x - hPadding, floatingInfoBox.position.y - vPadding, floatingInfoBox.size.width + (hPadding * 2), floatingInfoBox.size.height + (vPadding * 2));
	DrawHalfTransparentVerticalLine(out, { floatingInfoBox.position.x - hPadding - 1, floatingInfoBox.position.y - vPadding - 1 }, floatingInfoBox.size.height + (vPadding * 2) + 2, PAL16_GRAY + 10);
	DrawHalfTransparentVerticalLine(out, { floatingInfoBox.position.x + hPadding + floatingInfoBox.size.width, floatingInfoBox.position.y - vPadding - 1 }, floatingInfoBox.size.height + (vPadding * 2) + 2, PAL16_GRAY + 10);
	DrawHalfTransparentHorizontalLine(out, { floatingInfoBox.position.x - hPadding, floatingInfoBox.position.y - vPadding - 1 }, floatingInfoBox.size.width + (hPadding * 2), PAL16_GRAY + 10);
	DrawHalfTransparentHorizontalLine(out, { floatingInfoBox.position.x - hPadding, floatingInfoBox.position.y + vPadding + floatingInfoBox.size.height }, floatingInfoBox.size.width + (hPadding * 2), PAL16_GRAY + 10);

#ifdef __3DS__
	const Surface boxSurface = out.subregion(floatingInfoBox.position.x, floatingInfoBox.position.y,
	    floatingInfoBox.size.width, floatingInfoBox.size.height);
	DrawString(boxSurface, wrappedFloating, { { 0, -scroll }, { floatingInfoBox.size.width, textHeight } },
	    { .flags = InfoColor | UiFlags::AlignCenter, .spacing = textSpacing, .lineHeight = lineHeight });
#else
	DrawString(out, wrappedFloating, floatingInfoBox,
	    {
	        .flags = InfoColor | UiFlags::AlignCenter | UiFlags::VerticalCenter,
	        .spacing = textSpacing,
	        .lineHeight = lineHeight,
	    });
#endif
}

} // namespace

void AddInfoBoxString(std::string_view str, bool floatingBox /*= false*/)
{
	StringOrView &infoString = floatingBox ? FloatingInfoString : InfoString;

	if (infoString.empty())
		infoString = str;
	else
		infoString = StrCat(infoString, "\n", str);
}

void AddInfoBoxString(std::string &&str, bool floatingBox /*= false*/)
{
	StringOrView &infoString = floatingBox ? FloatingInfoString : InfoString;

	if (infoString.empty())
		infoString = std::move(str);
	else
		infoString = StrCat(infoString, "\n", str);
}

void CheckPanelInfo()
{
	MainPanelFlag = false;
	InfoString = StringOrView {};
	FloatingInfoString = StringOrView {};

#ifdef __3DS__
	if (MousePosition.y >= 240) {
		pcursinvitem = CheckInvHLight();
		if (pcursinvitem >= INVITEM_BELT_FIRST && pcursinvitem < INVITEM_BELT_FIRST + MaxBeltItems) {
			const Item &item = MyPlayer->SpdList[pcursinvitem - INVITEM_BELT_FIRST];
			if (!item.isEmpty()) {
				InfoString = item.getName();
				InfoColor = item.getTextColor();
			}
		}
		if (CheckXPBarInfo())
			MainPanelFlag = true;
		return;
	}
#endif

	const int totalButtons = IsChatAvailable() ? TotalMpMainPanelButtons : TotalSpMainPanelButtons;

	for (int i = 0; i < totalButtons; i++) {
		Rectangle button = MainPanelButtonRect[i];

		SetPanelObjectPosition(UiPanels::Main, button);

		if (button.contains(MousePosition)) {
			if (i != 7) {
				InfoString = _(PanBtnStr[i]);
			} else {
				if (MyPlayer->friendlyMode)
					InfoString = _("Player friendly");
				else
					InfoString = _("Player attack");
			}
			if (PanBtnHotKey[i] != nullptr) {
				AddInfoBoxString(FormatRuntime(_("Hotkey: {:s}"), _(PanBtnHotKey[i])));
			}
			InfoColor = UiFlags::ColorWhite;
			MainPanelFlag = true;
		}
	}

	Rectangle spellSelectButton = SpellButtonRect;

	SetPanelObjectPosition(UiPanels::Main, spellSelectButton);

	if (!SpellSelectFlag && spellSelectButton.contains(MousePosition)) {
		InfoString = _("Select current spell button");
		InfoColor = UiFlags::ColorWhite;
		MainPanelFlag = true;
		std::string_view speedbookKeyName = ControlMode == ControlTypes::Gamepad
		    ? GetOptions().Padmapper.InputNameForAction("DisplaySpells", true)
		    : GetOptions().Keymapper.KeyNameForAction("DisplaySpells");
		if (!speedbookKeyName.empty()) {
			AddInfoBoxString(FormatRuntime(_("Hotkey: '{:s}'"), speedbookKeyName));
		}
		const Player &myPlayer = *MyPlayer;
		const SpellID spellId = myPlayer._pRSpell;
		if (IsValidSpell(spellId)) {
			switch (myPlayer._pRSplType) {
			case SpellType::Skill:
				AddInfoBoxString(FormatRuntime(_("{:s} Skill"), pgettext("spell", GetSpellData(spellId).sNameText)));
				break;
			case SpellType::Spell: {
				AddInfoBoxString(FormatRuntime(_("{:s} Spell"), pgettext("spell", GetSpellData(spellId).sNameText)));
				const int spellLevel = myPlayer.GetSpellLevel(spellId);
				AddInfoBoxString(spellLevel == 0 ? _("Spell Level 0 - Unusable") : FormatRuntime(_("Spell Level {:d}"), spellLevel));
			} break;
			case SpellType::Scroll: {
				AddInfoBoxString(FormatRuntime(_("Scroll of {:s}"), pgettext("spell", GetSpellData(spellId).sNameText)));
				const int scrollCount = c_count_if(InventoryAndBeltPlayerItemsRange { myPlayer }, [spellId](const Item &item) {
					return item.isScrollOf(spellId);
				});
				AddInfoBoxString(FormatRuntime(ngettext("{:d} Scroll", "{:d} Scrolls", scrollCount), scrollCount));
			} break;
			case SpellType::Charges:
				AddInfoBoxString(FormatRuntime(_("Staff of {:s}"), pgettext("spell", GetSpellData(spellId).sNameText)));
				AddInfoBoxString(FormatRuntime(ngettext("{:d} Charge", "{:d} Charges", myPlayer.InvBody[INVLOC_HAND_LEFT]._iCharges), myPlayer.InvBody[INVLOC_HAND_LEFT]._iCharges));
				break;
			case SpellType::Invalid:
				break;
			}
		}
	}

	Rectangle belt = BeltRect;

	SetPanelObjectPosition(UiPanels::Main, belt);

	if (belt.contains(MousePosition))
		pcursinvitem = CheckInvHLight();

	if (CheckXPBarInfo())
		MainPanelFlag = true;
}

void DrawInfoBox(const Surface &out)
{
#ifndef __3DS__
	DrawPanelBox(out, MakeSdlRect(InfoBoxRect.position.x, InfoBoxRect.position.y + PanelPaddingHeight, InfoBoxRect.size.width, InfoBoxRect.size.height), GetMainPanel().position + Displacement { InfoBoxRect.position.x, InfoBoxRect.position.y });
#endif
	if (!MainPanelFlag && !trigflag && pcursinvitem == -1 && pcursstashitem == StashStruct::EmptyCell && pcursstoreitem == -1 && pcursstorebtn == -1 && !SpellSelectFlag && pcurs != CURSOR_HOURGLASS) {
		InfoString = StringOrView {};
		InfoColor = UiFlags::ColorWhite;
	}
	const Player &myPlayer = *MyPlayer;
	if (SpellSelectFlag || trigflag || pcurs == CURSOR_HOURGLASS) {
		InfoColor = UiFlags::ColorWhite;
	} else if (!myPlayer.HoldItem.isEmpty()) {
		if (myPlayer.HoldItem._itype == ItemType::Gold) {
			const int nGold = myPlayer.HoldItem._ivalue;
			InfoString = FormatRuntime(ngettext("{:s} gold piece", "{:s} gold pieces", nGold), FormatInteger(nGold));
		} else if (!myPlayer.CanUseItem(myPlayer.HoldItem)) {
			InfoString = _("Requirements not met");
		} else {
			InfoString = myPlayer.HoldItem.getName();
			InfoColor = myPlayer.HoldItem.getTextColor();
		}
	} else {
		if (pcursitem != -1)
			GetItemStr(Items[pcursitem]);
		else if (ObjectUnderCursor != nullptr)
			GetObjectStr(*ObjectUnderCursor);
		if (pcursmonst != -1) {
			if (leveltype != DTYPE_TOWN) {
				const Monster &monster = Monsters[pcursmonst];
				InfoColor = UiFlags::ColorWhite;
				InfoString = monster.name();
				if (monster.isUnique()) {
					InfoColor = UiFlags::ColorWhitegold;
					PrintUniqueHistory();
				} else {
					PrintMonstHistory(monster.type().type);
				}
			} else if (pcursitem == -1) {
				InfoString = std::string_view(Towners[pcursmonst].name);
			}
		}
		if (PlayerUnderCursor != nullptr) {
			InfoColor = UiFlags::ColorWhitegold;
			const auto &target = *PlayerUnderCursor;
			InfoString = std::string_view(target._pName);
			AddInfoBoxString(FormatRuntime(_("{:s}, Level: {:d}"), target.getClassName(), target.getCharacterLevel()));
			AddInfoBoxString(FormatRuntime(_("Hit Points {:d} of {:d}"), target._pHitPoints >> 6, target._pMaxHP >> 6));
		}
		if (PortraitIdUnderCursor != -1) {
			InfoColor = UiFlags::ColorWhitegold;
			auto &target = Players[PortraitIdUnderCursor];
			InfoString = std::string_view(target._pName);
			AddInfoBoxString(_("Right click to inspect"));
		}
	}
	if (!InfoString.empty())
		PrintInfo(out);
#ifdef __3DS__
	if (!InfoString.empty() || IsLeftPanelOpen() || IsRightPanelOpen()
	    || SDL_GetTicks() - GameStartHintTime >= 10000)
		ShowGameStartHint = false;
	if (InfoString.empty() && ShowGameStartHint)
		DrawString(out, _("SELECT: Open panels\nSTART: Meniu"), CtrBottomInfoBox,
		    { .flags = UiFlags::ColorWhitegold | UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::KerningFitSpacing,
		      .spacing = 1, .lineHeight = 18 });
#endif
}

void DrawFloatingInfoBox(const Surface &out)
{
	if (pcursinvitem == -1 && pcursstashitem == StashStruct::EmptyCell && pcursstoreitem == -1 && pcursstorebtn == -1) {
		FloatingInfoString = StringOrView {};
		InfoColor = UiFlags::ColorWhite;
	}

	if (!FloatingInfoString.empty())
		PrintFloatingInfo(out);
}

} // namespace devilution
