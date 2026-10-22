#include "panels/spell_list.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>

#include "control/control.hpp"
#include "controls/control_mode.hpp"
#include "controls/plrctrls.h"
#include "engine/backbuffer_state.hpp"
#include "engine/palette.h"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "inv_iterators.hpp"
#include "options.h"
#include "panels/spell_icons.hpp"
#include "player.h"
#include "spells.h"
#include "utils/algorithm/container.hpp"
#include "utils/display.h"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

#ifdef __3DS__
#include "platform/ctr/native_blit.hpp"
#include "platform/ctr/spell_geometry.hpp"
#endif

#define SPLROWICONLS 10

namespace devilution {

namespace {

void PrintSBookSpellType(const Surface &out, Point position, std::string_view text, uint8_t rectColorIndex)
{
	DrawLargeSpellIconBorder(out, position, rectColorIndex);

	// Align the spell type text with bottom of spell icon
	position += Displacement { (SPLICONLENGTH / 2) - (GetLineWidth(text) / 2), (IsSmallFontTall() ? -19 : -15) };

	// Then draw the text over the top
	DrawString(out, text, position, { .flags = UiFlags::ColorWhite | UiFlags::Outlined });
}

void PrintSBookHotkey(const Surface &out, Point position, const std::string_view text)
{
	// Align the hot key text with the top-right corner of the spell icon
	position += Displacement { SPLICONLENGTH - (GetLineWidth(text.data()) + 5), 5 - SPLICONLENGTH };

	// Then draw the text over the top
	DrawString(out, text, position, { .flags = UiFlags::ColorWhite | UiFlags::Outlined });
}

bool GetSpellListSelection(SpellID &pSpell, SpellType &pSplType)
{
	pSpell = SpellID::Invalid;
	pSplType = SpellType::Invalid;
	const Player &myPlayer = *MyPlayer;

	for (auto &spellListItem : GetSpellListItems()) {
		if (spellListItem.isSelected) {
			pSpell = spellListItem.id;
			pSplType = spellListItem.type;
			if (spellListItem.id == GetPlayerStartingLoadoutForClass(myPlayer._pClass).skill)
				pSplType = SpellType::Skill;
			return true;
		}
	}

	return false;
}

std::optional<std::string_view> GetHotkeyName(SpellID spellId, SpellType spellType, bool useShortName = false)
{
	const Player &myPlayer = *MyPlayer;
	for (size_t t = 0; t < NumHotkeys; t++) {
		if (myPlayer._pSplHotKey[t] != spellId || myPlayer._pSplTHotKey[t] != spellType)
			continue;
		auto quickSpellActionKey = StrCat("QuickSpell", t + 1);
		if (ControlMode == ControlTypes::Gamepad)
			return GetOptions().Padmapper.InputNameForAction(quickSpellActionKey, useShortName);
		return GetOptions().Keymapper.KeyNameForAction(quickSpellActionKey);
	}
	return {};
}

#ifdef __3DS__
constexpr std::array<SpellType, 4> SpellRowTypes {
	SpellType::Spell, SpellType::Charges, SpellType::Skill, SpellType::Scroll
};
std::array<size_t, 4> SpellRowFirst {};
constexpr int SpellListMargin = 8;
static_assert(CtrSpellIconSize == SPLICONLENGTH);

size_t SpellRowIndex(SpellType type)
{
	return static_cast<size_t>(std::find(SpellRowTypes.begin(), SpellRowTypes.end(), type) - SpellRowTypes.begin());
}

Point SpellItemCenter(const SpellListItem &item)
{
	const int x = CtrInverseColumn(item.location.x, 400);
	return { CtrNativeColumn(x + CtrSpellIconSize / 2, 400), item.location.y - CtrSpellIconSize / 2 };
}

void Draw3dsSpellHudIcon(const Surface &out, SpellID spell, SpellType type, Point position, bool selected = false)
{
	static OwnedSurface iconSurface(SPLICONLENGTH, SPLICONLENGTH);
	FillRect(iconSurface, 0, 0, SPLICONLENGTH, SPLICONLENGTH, 0);
	const Point iconPosition { 0, SPLICONLENGTH - 1 };
	SetSpellTrans(type);
	DrawLargeSpellIcon(iconSurface, iconPosition, spell);
	if (std::optional<std::string_view> hotkeyName = GetHotkeyName(spell, type, true))
		PrintSBookHotkey(iconSurface, iconPosition, *hotkeyName);
	if (selected)
		DrawLargeSpellIconBorder(iconSurface, iconPosition, PAL16_YELLOW - 46);
	CtrBlitTopNative(out, iconSurface, position);
}
#endif

} // namespace

void DrawSpell(const Surface &out)
{
	const Player &myPlayer = *MyPlayer;
	SpellID spl = myPlayer._pRSpell;
	SpellType st = myPlayer._pRSplType;

	if (!IsValidSpell(spl)) {
		st = SpellType::Invalid;
		spl = SpellID::Null;
	}

	if (st == SpellType::Spell) {
		const int tlvl = myPlayer.GetSpellLevel(spl);
		if (CheckSpell(*MyPlayer, spl, st, true) != SpellCheckResult::Success)
			st = SpellType::Invalid;
		if (tlvl <= 0)
			st = SpellType::Invalid;
	}

	if (leveltype == DTYPE_TOWN && st != SpellType::Invalid && !GetSpellData(spl).isAllowedInTown())
		st = SpellType::Invalid;

	SetSpellTrans(st);
#ifdef __3DS__
	if (SpellSelectFlag)
		return; // Selection already shows the same full-size icon.
	Draw3dsSpellHudIcon(out, spl, st,
	    { 400 - SpellListMargin - CtrSpellIconSize, 240 - SpellListMargin - CtrSpellIconSize });
#else
	const Point position = GetMainPanel().position + Displacement { 565, 119 };
	DrawLargeSpellIcon(out, position, spl);

	std::optional<std::string_view> hotkeyName = GetHotkeyName(spl, myPlayer._pRSplType, true);
	if (hotkeyName)
		PrintSBookHotkey(out, position, *hotkeyName);
#endif
}

void DrawSpellList(const Surface &out)
{
	InfoString = StringOrView {};

	const Player &myPlayer = *MyPlayer;

	const auto items = GetSpellListItems();
	for (const auto &spellListItem : items) {
		if (!spellListItem.isVisible)
			continue;
		const SpellID spellId = spellListItem.id;
		SpellType transType = spellListItem.type;
		int spellLevel = 0;
		const SpellData &spellDataItem = GetSpellData(spellListItem.id);
		if (leveltype == DTYPE_TOWN && !spellDataItem.isAllowedInTown()) {
			transType = SpellType::Invalid;
		}
		if (spellListItem.type == SpellType::Spell) {
			spellLevel = myPlayer.GetSpellLevel(spellListItem.id);
			if (spellLevel == 0)
				transType = SpellType::Invalid;
		}

		SetSpellTrans(transType);
#ifdef __3DS__
		Draw3dsSpellHudIcon(out, spellId, transType,
		    { CtrInverseColumn(spellListItem.location.x, 400), spellListItem.location.y - CtrSpellIconSize }, spellListItem.isSelected);
#else
		DrawLargeSpellIcon(out, spellListItem.location, spellId);
#endif

#ifndef __3DS__
		std::optional<std::string_view> shortHotkeyName = GetHotkeyName(spellId, spellListItem.type, true);
		if (shortHotkeyName)
			PrintSBookHotkey(out, spellListItem.location, *shortHotkeyName);
#endif

		if (!spellListItem.isSelected)
			continue;

#ifndef __3DS__
		uint8_t spellColor = PAL16_GRAY + 5;
#endif

		switch (spellListItem.type) {
		case SpellType::Skill:
#ifndef __3DS__
			spellColor = PAL16_YELLOW - 46;
			PrintSBookSpellType(out, spellListItem.location, _("Skill"), spellColor);
#endif
			InfoString = FormatRuntime(_("{:s} Skill"), pgettext("spell", spellDataItem.sNameText));
			break;
		case SpellType::Spell:
#ifndef __3DS__
			if (!myPlayer.isOnLevel(0)) {
				spellColor = PAL16_BLUE + 5;
			}
			PrintSBookSpellType(out, spellListItem.location, _("Spell"), spellColor);
#endif
			InfoString = FormatRuntime(_("{:s} Spell"), pgettext("spell", spellDataItem.sNameText));
			if (spellId == SpellID::HolyBolt) {
				AddInfoBoxString(_("Damages undead only"));
			}
			if (spellLevel == 0)
				AddInfoBoxString(_("Spell Level 0 - Unusable"));
			else
				AddInfoBoxString(FormatRuntime(_("Spell Level {:d}"), spellLevel));
			break;
		case SpellType::Scroll: {
#ifndef __3DS__
			if (!myPlayer.isOnLevel(0)) {
				spellColor = PAL16_RED - 59;
			}
			PrintSBookSpellType(out, spellListItem.location, _("Scroll"), spellColor);
#endif
			InfoString = FormatRuntime(_("Scroll of {:s}"), pgettext("spell", spellDataItem.sNameText));
			const int scrollCount = c_count_if(InventoryAndBeltPlayerItemsRange { myPlayer }, [spellId](const Item &item) {
				return item.isScrollOf(spellId);
			});
			AddInfoBoxString(FormatRuntime(ngettext("{:d} Scroll", "{:d} Scrolls", scrollCount), scrollCount));
		} break;
		case SpellType::Charges: {
#ifndef __3DS__
			if (!myPlayer.isOnLevel(0)) {
				spellColor = PAL16_ORANGE + 5;
			}
			PrintSBookSpellType(out, spellListItem.location, _("Staff"), spellColor);
#endif
			InfoString = FormatRuntime(_("Staff of {:s}"), pgettext("spell", spellDataItem.sNameText));
			int charges = myPlayer.InvBody[INVLOC_HAND_LEFT]._iCharges;
			AddInfoBoxString(FormatRuntime(ngettext("{:d} Charge", "{:d} Charges", charges), charges));
		} break;
		case SpellType::Invalid:
			break;
		}
		std::optional<std::string_view> fullHotkeyName = GetHotkeyName(spellId, spellListItem.type);
		if (fullHotkeyName) {
			AddInfoBoxString(FormatRuntime(_("Spell Hotkey {:s}"), *fullHotkeyName));
		}
	}
#ifdef __3DS__
	for (size_t typeIndex = 0; typeIndex < SpellRowTypes.size(); ++typeIndex) {
		const SpellType type = SpellRowTypes[typeIndex];
		const size_t count = std::count_if(items.begin(), items.end(), [type](const SpellListItem &item) { return item.type == type; });
		if (count <= CtrSpellColumns) continue;
		const auto first = std::find_if(items.begin(), items.end(), [type](const SpellListItem &item) { return item.type == type && item.isVisible; });
		if (first == items.end()) continue;
		const int y = first->location.y - CtrSpellIconSize / 2 - 6;
		const auto drawArrow = [&](const char *text, int x) {
			DrawString(out, text, { { CtrNativeColumn(x, 400), y }, { CtrNativeColumn(20, 400), 16 } },
			    { .flags = UiFlags::AlignCenter | UiFlags::ColorGold, .spacing = 0 });
		};
		if (SpellRowFirst[typeIndex] != 0) drawArrow("<", 0);
		if (SpellRowFirst[typeIndex] + CtrSpellColumns < count) drawArrow(">", 380);
	}
#endif
}

std::vector<SpellListItem> GetSpellListItems()
{
	std::vector<SpellListItem> spellListItems;

	uint64_t mask;
#ifndef __3DS__
	const Point mainPanelPosition = GetMainPanel().position;
	int x = mainPanelPosition.x + 12 + (SPLICONLENGTH * SPLROWICONLS);
	int y = mainPanelPosition.y - 17;
#endif

	for (auto i : enum_values<SpellType>()) {
		const Player &myPlayer = *MyPlayer;
		switch (static_cast<SpellType>(i)) {
		case SpellType::Skill:
			mask = myPlayer._pAblSpells;
			break;
		case SpellType::Spell:
			mask = myPlayer._pMemSpells;
			break;
		case SpellType::Scroll:
			mask = myPlayer._pScrlSpells;
			break;
		case SpellType::Charges:
			mask = myPlayer._pISpells;
			break;
		default:
			continue;
		}
		auto j = static_cast<int8_t>(SpellID::Firebolt);
		for (uint64_t spl = 1; static_cast<size_t>(j) < SpellsData.size(); spl <<= 1, j++) {
			if ((mask & spl) == 0)
				continue;
#ifdef __3DS__
			spellListItems.emplace_back(SpellListItem {
				{ -1000, -1000 }, static_cast<SpellType>(i), static_cast<SpellID>(j), false, false
			});
#else
			const int lx = x;
			const int ly = y - SPLICONLENGTH;
			const bool isSelected = (MousePosition.x >= lx && MousePosition.x < lx + SPLICONLENGTH && MousePosition.y >= ly && MousePosition.y < ly + SPLICONLENGTH);
			spellListItems.emplace_back(SpellListItem { { x, y }, static_cast<SpellType>(i), static_cast<SpellID>(j), isSelected });
			x -= SPLICONLENGTH;
			if (x == mainPanelPosition.x + 12 - SPLICONLENGTH) {
				x = mainPanelPosition.x + 12 + SPLICONLENGTH * SPLROWICONLS;
				y -= SPLICONLENGTH;
			}
#endif
		}
#ifndef __3DS__
		if (mask != 0 && x != mainPanelPosition.x + 12 + SPLICONLENGTH * SPLROWICONLS)
			x -= SPLICONLENGTH;
		if (x == mainPanelPosition.x + 12 - SPLICONLENGTH) {
			x = mainPanelPosition.x + 12 + SPLICONLENGTH * SPLROWICONLS;
			y -= SPLICONLENGTH;
		}
#endif
	}

#ifdef __3DS__
	std::array<size_t, 4> counts {};
	for (const auto &item : spellListItems)
		++counts[SpellRowIndex(item.type)];
	const int rows = static_cast<int>(std::count_if(counts.begin(), counts.end(), [](size_t n) { return n != 0; }));
	int row = 0;
	for (size_t typeIndex = 0; typeIndex < SpellRowTypes.size(); ++typeIndex) {
		const size_t count = counts[typeIndex];
		if (count == 0) continue;
		size_t &first = SpellRowFirst[typeIndex];
		first = std::min(first, count > CtrSpellColumns ? count - CtrSpellColumns : 0);
		size_t index = 0;
		for (auto &item : spellListItems) {
			if (item.type != SpellRowTypes[typeIndex]) continue;
			if (index >= first && index < first + CtrSpellColumns) {
				const Rectangle rect = CtrSpellRect(static_cast<int>(index - first), row, rows);
				const int x = CtrNativeColumn(rect.position.x, 400);
				const int right = CtrNativeColumn(rect.position.x + CtrSpellIconSize, 400);
				item.location = { x, rect.position.y + CtrSpellIconSize };
				item.isVisible = true;
				item.isSelected = MousePosition.x >= x && MousePosition.x < right
				    && MousePosition.y >= rect.position.y && MousePosition.y < item.location.y;
			}
			++index;
		}
		++row;
	}
#endif
	return spellListItems;
}

#ifdef __3DS__
bool Focus3dsSpellListItem(SpellID spell, SpellType type)
{
	const auto items = GetSpellListItems();
	size_t index = 0, count = 0;
	bool found = false;
	for (const auto &item : items) {
		if (item.type != type) continue;
		if (item.id == spell) { index = count; found = true; }
		++count;
	}
	if (!found) return false;
	const size_t row = SpellRowIndex(type);
	SpellRowFirst[row] = CtrSpellScrollFirst(index, SpellRowFirst[row], count);
	for (const auto &item : GetSpellListItems()) {
		if (item.type == type && item.id == spell && item.isVisible) {
			SetCursorPos(SpellItemCenter(item));
			return true;
		}
	}
	return false;
}

void Move3dsSpellListSelection(AxisDirection dir)
{
	const auto items = GetSpellListItems();
	const SpellListItem *current = nullptr;
	int nearest = std::numeric_limits<int>::max();
	for (const auto &item : items) {
		if (!item.isVisible) continue;
		const int distance = MousePosition.ManhattanDistance(SpellItemCenter(item));
		if (distance < nearest) { nearest = distance; current = &item; }
	}
	if (current == nullptr) return;
	const SpellListItem *target = current;
	if (dir.y != AxisDirectionY_NONE) {
		const Point origin = SpellItemCenter(*current);
		int best = std::numeric_limits<int>::max();
		for (const auto &item : items) {
			if (!item.isVisible) continue;
			const Point center = SpellItemCenter(item);
			const int dy = center.y - origin.y;
			if ((dir.y == AxisDirectionY_UP && dy >= 0) || (dir.y == AxisDirectionY_DOWN && dy <= 0)) continue;
			const int distance = std::abs(dy) * 640 + std::abs(center.x - origin.x);
			if (distance < best) { best = distance; target = &item; }
		}
	} else if (dir.x != AxisDirectionX_NONE) {
		const SpellListItem *previous = nullptr;
		for (const auto &item : items) {
			if (item.type != current->type) continue;
			if (&item == current) {
				if (dir.x == AxisDirectionX_LEFT && previous != nullptr) target = previous;
				if (dir.x == AxisDirectionX_RIGHT) {
					const auto next = std::find_if(&item + 1, items.data() + items.size(), [current](const SpellListItem &entry) { return entry.type == current->type; });
					if (next != items.data() + items.size()) target = next;
				}
				break;
			}
			previous = &item;
		}
	}
	Focus3dsSpellListItem(target->id, target->type);
}
#endif

void SetSpell()
{
	SpellID pSpell;
	SpellType pSplType;

	SpellSelectFlag = false;
	if (!GetSpellListSelection(pSpell, pSplType)) {
		return;
	}

	Player &myPlayer = *MyPlayer;
	myPlayer._pRSpell = pSpell;
	myPlayer._pRSplType = pSplType;

	RedrawEverything();
}

void SetSpeedSpell(size_t slot)
{
	SpellID pSpell;
	SpellType pSplType;

	if (!GetSpellListSelection(pSpell, pSplType)) {
		return;
	}
	Player &myPlayer = *MyPlayer;

	if (myPlayer._pSplHotKey[slot] == pSpell && myPlayer._pSplTHotKey[slot] == pSplType) {
		// Unset spell hotkey
		myPlayer._pSplHotKey[slot] = SpellID::Invalid;
		return;
	}

	for (size_t i = 0; i < NumHotkeys; ++i) {
		if (myPlayer._pSplHotKey[i] == pSpell && myPlayer._pSplTHotKey[i] == pSplType)
			myPlayer._pSplHotKey[i] = SpellID::Invalid;
	}
	myPlayer._pSplHotKey[slot] = pSpell;
	myPlayer._pSplTHotKey[slot] = pSplType;
}

bool IsValidSpeedSpell(size_t slot)
{
	uint64_t spells;

	const Player &myPlayer = *MyPlayer;

	const SpellID spellId = myPlayer._pSplHotKey[slot];
	if (!IsValidSpell(spellId)) {
		return false;
	}

	switch (myPlayer._pSplTHotKey[slot]) {
	case SpellType::Skill:
		spells = myPlayer._pAblSpells;
		break;
	case SpellType::Spell:
		spells = myPlayer._pMemSpells;
		break;
	case SpellType::Scroll:
		spells = myPlayer._pScrlSpells;
		break;
	case SpellType::Charges:
		spells = myPlayer._pISpells;
		break;
	case SpellType::Invalid:
		return false;
	}

	return (spells & GetSpellBitmask(spellId)) != 0;
}

void ToggleSpell(size_t slot)
{
	if (IsValidSpeedSpell(slot)) {
		Player &myPlayer = *MyPlayer;
		myPlayer._pRSpell = myPlayer._pSplHotKey[slot];
		myPlayer._pRSplType = myPlayer._pSplTHotKey[slot];
		RedrawEverything();
	}
}

void DoSpeedBook()
{
	SpellSelectFlag = true;
#ifdef __3DS__
	SpellRowFirst.fill(0);
	if (!Focus3dsSpellListItem(MyPlayer->_pRSpell, MyPlayer->_pRSplType)) {
		for (SpellType type : SpellRowTypes) {
			const auto items = GetSpellListItems();
			const auto item = std::find_if(items.begin(), items.end(), [type](const SpellListItem &entry) { return entry.type == type; });
			if (item != items.end()) {
				Focus3dsSpellListItem(item->id, item->type);
				break;
			}
		}
	}
#else
	const Point mainPanelPosition = GetMainPanel().position;
	int xo = mainPanelPosition.x + 12 + (SPLICONLENGTH * 10);
	int yo = mainPanelPosition.y - 17;
	int x = xo + (SPLICONLENGTH / 2);
	int y = yo - (SPLICONLENGTH / 2);

	const Player &myPlayer = *MyPlayer;

	if (IsValidSpell(myPlayer._pRSpell)) {
		for (auto i : enum_values<SpellType>()) {
			uint64_t spells;
			switch (static_cast<SpellType>(i)) {
			case SpellType::Skill:
				spells = myPlayer._pAblSpells;
				break;
			case SpellType::Spell:
				spells = myPlayer._pMemSpells;
				break;
			case SpellType::Scroll:
				spells = myPlayer._pScrlSpells;
				break;
			case SpellType::Charges:
				spells = myPlayer._pISpells;
				break;
			default:
				continue;
			}
			uint64_t spell = 1;
			for (size_t j = 1; j < SpellsData.size(); j++) {
				if ((spell & spells) != 0) {
					if (j == static_cast<size_t>(myPlayer._pRSpell) && static_cast<SpellType>(i) == myPlayer._pRSplType) {
						x = xo + SPLICONLENGTH / 2;
						y = yo - SPLICONLENGTH / 2;
					}
					xo -= SPLICONLENGTH;
					if (xo == mainPanelPosition.x + 12 - SPLICONLENGTH) {
						xo = mainPanelPosition.x + 12 + SPLICONLENGTH * SPLROWICONLS;
						yo -= SPLICONLENGTH;
					}
				}
				spell <<= 1ULL;
			}
			if (spells != 0 && xo != mainPanelPosition.x + 12 + SPLICONLENGTH * SPLROWICONLS)
				xo -= SPLICONLENGTH;
			if (xo == mainPanelPosition.x + 12 - SPLICONLENGTH) {
				xo = mainPanelPosition.x + 12 + SPLICONLENGTH * SPLROWICONLS;
				yo -= SPLICONLENGTH;
			}
		}
	}

	SetCursorPos({ x, y });
#endif
}

} // namespace devilution
