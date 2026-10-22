#include "panels/spell_book.hpp"

#ifdef __3DS__
#include "platform/ctr/ui.hpp"
#include "platform/ctr/pixel_geometry.hpp"
#endif

#include <algorithm>
#include <vector>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include "control/control.hpp"
#include "diablo.h"
#include "engine/backbuffer_state.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/load_cel.hpp"
#include "engine/load_clx.hpp"
#include "engine/rectangle.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/text_render.hpp"
#include "game_mode.hpp"
#include "missiles.h"
#include "panels/spell_icons.hpp"
#include "panels/ui_panels.hpp"
#include "player.h"
#include "tables/spelldat.h"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/status_macros.hpp"

namespace devilution {

namespace {

OptionalOwnedClxSpriteList spellBookButtons;
OptionalOwnedClxSpriteList spellBookBackground;

const size_t SpellBookPages = 6;
const size_t SpellBookPageEntries = 7;

constexpr uint16_t SpellBookButtonWidthDiablo = 76;
constexpr uint16_t SpellBookButtonWidthHellfire = 61;

uint16_t SpellBookButtonWidth()
{
	return gbIsHellfire ? SpellBookButtonWidthHellfire : SpellBookButtonWidthDiablo;
}

/** Maps from spellbook page number and position to SpellID. */
const SpellID SpellPages[SpellBookPages][SpellBookPageEntries] = {
	{ SpellID::Null, SpellID::Firebolt, SpellID::ChargedBolt, SpellID::HolyBolt, SpellID::Healing, SpellID::HealOther, SpellID::Inferno },
	{ SpellID::Resurrect, SpellID::FireWall, SpellID::Telekinesis, SpellID::Lightning, SpellID::TownPortal, SpellID::Flash, SpellID::StoneCurse },
	{ SpellID::Phasing, SpellID::ManaShield, SpellID::Elemental, SpellID::Fireball, SpellID::FlameWave, SpellID::ChainLightning, SpellID::Guardian },
	{ SpellID::Nova, SpellID::Golem, SpellID::Teleport, SpellID::Apocalypse, SpellID::BoneSpirit, SpellID::BloodStar, SpellID::Etherealize },
	{ SpellID::LightningWall, SpellID::Immolation, SpellID::Warp, SpellID::Reflect, SpellID::Berserk, SpellID::RingOfFire, SpellID::Search },
	{ SpellID::Invalid, SpellID::Invalid, SpellID::Invalid, SpellID::Invalid, SpellID::Invalid, SpellID::Invalid, SpellID::Invalid }
};

#ifdef __3DS__
const std::vector<SpellID> &KnownCtrSpells()
{
	const Player &player = *InspectPlayer;
	const uint64_t known = player._pMemSpells | player._pISpells | player._pAblSpells;
	static std::vector<SpellID> result;
	static uint64_t cachedKnown = 0;
	static SpellID cachedSkill = SpellID::Invalid;
	const auto skill = GetPlayerStartingLoadoutForClass(player._pClass).skill;
	if (known == cachedKnown && skill == cachedSkill)
		return result;
	cachedKnown = known;
	cachedSkill = skill;
	result.clear();
	result.reserve(static_cast<size_t>(SpellID::LAST));
	const auto append = [&](SpellID spell) {
		if (IsValidSpell(spell) && spell != SpellID::Null && (known & GetSpellBitmask(spell)) != 0
		    && std::find(result.begin(), result.end(), spell) == result.end())
			result.push_back(spell);
	};
	append(GetPlayerStartingLoadoutForClass(player._pClass).skill);
	for (const auto &page : SpellPages)
		for (const auto spell : page)
			append(spell);
	// Include granted abilities and staff spells outside the original fixed pages.
	for (int spell = 1; spell <= static_cast<int>(SpellID::LAST); ++spell)
		append(static_cast<SpellID>(spell));
	return result;
}
#endif

SpellID GetSpellFromSpellPage(size_t page, size_t entry)
{
#ifdef __3DS__
	const auto &spells = KnownCtrSpells();
	if (entry >= CtrSpellRowsPerPage || page >= (spells.size() + CtrSpellRowsPerPage - 1) / CtrSpellRowsPerPage)
		return SpellID::Invalid;
	const size_t index = page * CtrSpellRowsPerPage + entry;
	return entry < CtrSpellRowsPerPage && index < spells.size() ? spells[index] : SpellID::Invalid;
#else
	if (page >= SpellBookPages || entry >= SpellBookPageEntries)
		return SpellID::Invalid;
	if (page == 0 && entry == 0)
		return GetPlayerStartingLoadoutForClass(InspectPlayer->_pClass).skill;
	return SpellPages[page][entry];
#endif
}

constexpr Size SpellBookDescription { 250, 43 };
constexpr int SpellBookDescriptionPaddingHorizontal = 2;

void PrintSBookStr(const Surface &out, Point position, std::string_view text, UiFlags flags = UiFlags::None)
{
	DrawString(out, text,
	    Rectangle(GetPanelPosition(UiPanels::Spell, position + Displacement { SPLICONLENGTH, 0 }),
	        SpellBookDescription)
	        .inset({ SpellBookDescriptionPaddingHorizontal, 0 }),
	    { .flags = UiFlags::ColorWhite | flags });
}

SpellType GetSBookTrans(SpellID ii, bool townok)
{
	const Player &player = *InspectPlayer;
	if (ii == GetPlayerStartingLoadoutForClass(player._pClass).skill)
		return SpellType::Skill;
	SpellType st = SpellType::Spell;
	if ((player._pISpells & GetSpellBitmask(ii)) != 0) {
		st = SpellType::Charges;
	}
	if ((player._pAblSpells & GetSpellBitmask(ii)) != 0) {
		st = SpellType::Skill;
	}
	if (st == SpellType::Spell) {
		if (CheckSpell(*InspectPlayer, ii, st, true) != SpellCheckResult::Success) {
			st = SpellType::Invalid;
		}
		if (player.GetSpellLevel(ii) == 0) {
			st = SpellType::Invalid;
		}
	}
	if (townok && leveltype == DTYPE_TOWN && st != SpellType::Invalid && !GetSpellData(ii).isAllowedInTown()) {
		st = SpellType::Invalid;
	}

	return st;
}

StringOrView GetSpellPowerText(SpellID spell, int spellLevel)
{
	if (spellLevel == 0) {
		return _("Unusable");
	}
	if (spell == SpellID::BoneSpirit) {
		return _(/* TRANSLATORS: UI constraints, keep short please.*/ "Dmg: 1/3 target hp");
	}
	const auto [min, max] = GetDamageAmt(spell, spellLevel);
	if (min == -1) {
		return StringOrView {};
	}
	if (spell == SpellID::Healing || spell == SpellID::HealOther) {
		return FormatRuntime(_(/* TRANSLATORS: UI constraints, keep short please.*/ "Heals: {:d} - {:d}"), min, max);
	}
	return FormatRuntime(_(/* TRANSLATORS: UI constraints, keep short please.*/ "Damage: {:d} - {:d}"), min, max);
}

#ifdef __3DS__
bool bindingOpen = false;
SpellID bindingSpell = SpellID::Invalid;
SpellType bindingType = SpellType::Invalid;

SpellType CtrSpellSource(SpellID spell)
{
	const auto mask = GetSpellBitmask(spell);
	if ((InspectPlayer->_pAblSpells & mask) != 0)
		return SpellType::Skill;
	if ((InspectPlayer->_pISpells & mask) != 0)
		return SpellType::Charges;
	return SpellType::Spell;
}

Rectangle CompactTabRect(int page)
{
	const int pages = CtrSpellbookPageCount();
	const int step = 368 / pages;
	return { { 16 + page * step, CtrSpellTabsY }, { step - 4, CtrSpellTabHeight } };
}

SpellID FocusedCtrSpell()
{
	const Point point = CtrScreenToTop(MousePosition);
	if (point.y >= CtrSpellRowsY && point.y < CtrSpellRowsY + CtrSpellRowsPerPage * CtrSpellRowHeight) {
		const auto spell = GetSpellFromSpellPage(SpellbookTab, (point.y - CtrSpellRowsY) / CtrSpellRowHeight);
		if (IsValidSpell(spell))
			return spell;
	}
	return GetSpellFromSpellPage(SpellbookTab, 0);
}

std::string_view SpellDescription(SpellID spell)
{
	switch (spell) {
	case SpellID::Firebolt: return _("Launches a small bolt of fire at an enemy.");
	case SpellID::Healing: return _("Restores your life.");
	case SpellID::Lightning: return _("Sends lightning through enemies in a line.");
	case SpellID::Flash: return _("Blasts enemies standing close to you.");
	case SpellID::Identify: return _("Reveals the magical properties of an item.");
	case SpellID::FireWall: return _("Creates a burning wall that hurts creatures crossing it.");
	case SpellID::TownPortal: return _("Opens a portal between the dungeon and town.");
	case SpellID::StoneCurse: return _("Turns an enemy to stone for a short time.");
	case SpellID::Infravision: return _("Reveals nearby creatures, even behind walls.");
	case SpellID::Phasing: return _("Teleports you a short distance to a random location.");
	case SpellID::ManaShield: return _("Uses mana to absorb damage instead of life.");
	case SpellID::Fireball: return _("Fires an exploding ball of fire.");
	case SpellID::Guardian: return _("Summons a guardian that spits fire at nearby enemies.");
	case SpellID::ChainLightning: return _("Sends bolts of lightning toward nearby enemies.");
	case SpellID::FlameWave: return _("Sweeps a wide wave of fire across the dungeon.");
	case SpellID::Nova: return _("Releases lightning in all directions.");
	case SpellID::Inferno: return _("Projects a short stream of fire.");
	case SpellID::Golem: return _("Summons a stone ally to fight for you.");
	case SpellID::Rage: return _("Temporarily boosts your fighting power, followed by exhaustion.");
	case SpellID::Teleport: return _("Moves you instantly to the selected location.");
	case SpellID::Apocalypse: return _("Strikes nearby enemies with powerful explosions.");
	case SpellID::Etherealize: return _("Temporarily makes you immune to physical attacks.");
	case SpellID::ItemRepair: return _("Repairs equipment, but may reduce its maximum durability.");
	case SpellID::StaffRecharge: return _("Restores staff charges, but may reduce its maximum charges.");
	case SpellID::TrapDisarm: return _("Attempts to disarm a trap.");
	case SpellID::Elemental: return _("Sends a fiery elemental toward an enemy.");
	case SpellID::ChargedBolt: return _("Releases several wandering sparks of lightning.");
	case SpellID::HolyBolt: return _("Fires a holy bolt effective against undead.");
	case SpellID::Resurrect: return _("Revives another player who has died.");
	case SpellID::Telekinesis: return _("Interacts with a distant item, door or object.");
	case SpellID::HealOther: return _("Restores another player's life.");
	case SpellID::BloodStar: return _("Fires a magical star, consuming mana and some life.");
	case SpellID::BoneSpirit: return _("Damages an enemy for a third of its current life.");
	case SpellID::Mana: return _("Restores mana.");
	case SpellID::Magi: return _("Fully restores mana.");
	case SpellID::Jester: return _("Unleashes an unpredictable magical effect.");
	case SpellID::LightningWall: return _("Creates a wall of crackling lightning.");
	case SpellID::Immolation: return _("Releases fireballs in all directions.");
	case SpellID::Warp: return _("Teleports you toward the nearest stairs.");
	case SpellID::Reflect: return _("Reflects some melee damage back at attackers.");
	case SpellID::Berserk: return _("Turns a monster against other monsters.");
	case SpellID::RingOfFire: return _("Creates a ring of fire around you.");
	case SpellID::Search: return _("Highlights nearby items on the ground.");
	case SpellID::RuneOfFire: return _("Places a fire trap on the ground.");
	case SpellID::RuneOfLight: return _("Places a lightning trap on the ground.");
	case SpellID::RuneOfNova: return _("Places a trap that releases a lightning nova.");
	case SpellID::RuneOfImmolation: return _("Places a trap that launches fireballs.");
	case SpellID::RuneOfStone: return _("Places a trap that turns its victim to stone.");
	default: return _("Select a spell or skill to see its description.");
	}
}

std::string CtrSpellShortcutText(SpellID spell, SpellType type)
{
	constexpr const char *labels[] = { "(X)", "(Y)", "(L)", "(R)" };
	const Player &player = *InspectPlayer;
	std::string result;
	for (int slot = 0; slot < CtrSpellShortcutCount; ++slot) {
		const SpellID assigned = slot == 0 ? player._pRSpell : player._pSplHotKey[slot];
		const SpellType source = slot == 0 ? player._pRSplType : player._pSplTHotKey[slot];
		if (assigned != spell || source != type)
			continue;
		if (!result.empty()) result += ' ';
		result += labels[slot];
	}
	return result;
}

void DrawCtrBinding(const Surface &out)
{
	DrawCtrPanelBackground(out, CtrPanelBackground::SpellBinding);
	DrawString(out, _("Choose spell or skill shortcut"), { { 16, 12 }, { 368, 16 } },
	    { .flags = UiFlags::ColorWhitegold | UiFlags::AlignCenter, .spacing = 0 });
	DrawString(out, pgettext("spell", GetSpellData(bindingSpell).sNameText), { { 16, 43 }, { 368, 14 } },
	    { .flags = UiFlags::ColorWhite | UiFlags::AlignCenter, .spacing = 0 });
	const int hoveredSlot = CtrSpellBindingHit(CtrScreenToTop(MousePosition));
	constexpr const char *labels[] = { "(X)", "(Y)", "(L)", "(R)" };
	for (size_t slot = 0; slot < CtrSpellShortcutCount; ++slot) {
		const Point p = CtrSpellBindingPositions[slot];
		FillRect(out, p.x, p.y, 37, 38, 0);
		SpellID spell = InspectPlayer->_pSplHotKey[slot];
		SpellType type = InspectPlayer->_pSplTHotKey[slot];
		if (slot == 0) {
			spell = InspectPlayer->_pRSpell;
			type = InspectPlayer->_pRSplType;
		}
		if (IsValidSpell(spell)) {
			SetSpellTrans(type);
			DrawSmallSpellIcon(out, p + Displacement { 0, 37 }, spell);
		}
		if (static_cast<int>(slot) == hoveredSlot) {
			DrawHalfTransparentRectTo(out, p.x, p.y, 37, 38, PAL16_RED + 7);
			UnsafeDrawBorder2px(out, CtrSpellBindingRect(slot), PAL16_RED + 3);
		}
		DrawString(out, labels[slot], { { p.x - 14, p.y + 41 }, { 65, 16 } },
		    { .flags = UiFlags::ColorWhite | UiFlags::AlignCenter });
	}
	DrawString(out, _("(A): Assign    (B): Cancel"), { { 16, 214 }, { 368, 16 } }, { .flags = UiFlags::ColorWhite | UiFlags::AlignCenter });
}

void DrawSpellBook3DS(const Surface &out)
{
	DrawCtrPanelBackground(out, CtrPanelBackground::Spells);
	SpellbookTab = std::clamp(SpellbookTab, 0, CtrSpellbookPageCount() - 1);
	if (bindingOpen) {
		DrawCtrBinding(out);
		return;
	}
	// Remove the artwork's original fixed page frames; draw only populated pages.
	out.BlitFrom(out, MakeSdlRect(10, 180, 380, 23), { 10, CtrSpellTabsY - 3 });
	DrawString(out, _("Spells & Skills"), { { 20, 14 }, { 360, 14 } },
	    { .flags = UiFlags::AlignCenter | UiFlags::ColorWhitegold });
	DrawString(out, _("(A): Select primary · (Y): Bind quick spell"), { { 20, 29 }, { 360, 14 } },
	    { .flags = UiFlags::AlignCenter | UiFlags::ColorWhite, .spacing = 0 });
	const Player &player = *InspectPlayer;
	const uint64_t spells = player._pMemSpells | player._pISpells | player._pAblSpells;
	int shortcutWidth = 28;
	for (size_t entry = 0; entry < CtrSpellRowsPerPage; ++entry) {
		const SpellID spell = GetSpellFromSpellPage(SpellbookTab, entry);
		if (IsValidSpell(spell))
			shortcutWidth = std::max(shortcutWidth, std::min(84, GetLineWidth(CtrSpellShortcutText(spell, CtrSpellSource(spell)), GameFont12, 0)));
	}
	const int iconX = 20 + shortcutWidth + 8;
	const int nameX = iconX + 34;
	const int nameWidth = 378 - nameX;
	const int detailWidth = nameWidth / 2 - 2;
	for (size_t entry = 0; entry < CtrSpellRowsPerPage; ++entry) {
		const SpellID spell = GetSpellFromSpellPage(SpellbookTab, entry);
		const int y = CtrSpellRowsY + entry * CtrSpellRowHeight;
		if (!IsValidSpell(spell) || (spells & GetSpellBitmask(spell)) == 0)
			continue;
		const SpellType type = GetSBookTrans(spell, true);
		const bool selected = spell == FocusedCtrSpell() && !IsInspectingPlayer();
		if (selected)
			DrawHalfTransparentRectTo(out, 20, y, 360, CtrSpellRowHeight, PAL16_RED + 7);
		const Surface icon = SidePanelBuffer->subregion(0, 0, 40, 40);
		FillRect(icon, 0, 0, 40, 40, 0);
		SetSpellTrans(type);
		DrawSmallSpellIcon(icon, { 0, 37 }, spell);
		if (selected) {
			SetSpellTrans(SpellType::Skill);
			DrawSmallSpellIconBorder(icon, { 0, 37 });
		}
		out.ScaleBlitFrom(icon, MakeSdlRect(0, 0, 37, 38), MakeSdlRect(iconX, y + 3, 26, 27));
		const SpellType infoType = CtrSpellSource(spell);
		std::string name { pgettext("spell", GetSpellData(spell).sNameText) };
		const std::string shortcuts = CtrSpellShortcutText(spell, infoType);
		DrawString(out, WordWrapString(shortcuts, shortcutWidth, GameFont12, 0),
		    { { 20, y }, { shortcutWidth, CtrSpellRowHeight } },
		    { .flags = UiFlags::ColorWhite | UiFlags::AlignCenter | UiFlags::VerticalCenter, .spacing = 0, .lineHeight = 12 });
		DrawString(out, WordWrapString(name, nameWidth, GameFont12, 0), { { nameX, y }, { nameWidth, 24 } },
		    { .flags = UiFlags::ColorWhite, .spacing = 0, .lineHeight = 12 });
		if (infoType == SpellType::Skill) {
			DrawString(out, _("Skill"), { { nameX, y + 24 }, { nameWidth, 12 } }, { .flags = UiFlags::ColorWhitegold });
		} else if (infoType == SpellType::Charges) {
			const int charges = player.InvBody[INVLOC_HAND_LEFT]._iCharges;
			DrawString(out, FormatRuntime(ngettext("Staff ({:d} charge)", "Staff ({:d} charges)", charges), charges), { { nameX, y + 24 }, { nameWidth, 12 } }, { .flags = UiFlags::ColorWhitegold });
		} else {
			const int level = player.GetSpellLevel(spell);
			DrawString(out, FormatRuntime(_("Lv {:d} · Mana: {:d}"), level, GetManaAmount(player, spell) >> 6), { { nameX, y + 24 }, { detailWidth, 12 } }, { .flags = UiFlags::ColorWhitegold | UiFlags::KerningFitSpacing, .spacing = 0 });
			const StringOrView power = GetSpellPowerText(spell, level);
			DrawString(out, power, { { nameX + detailWidth + 4, y + 24 }, { nameWidth - detailWidth - 4, 12 } }, { .flags = UiFlags::ColorWhitegold | UiFlags::AlignRight | UiFlags::KerningFitSpacing, .spacing = 0 });
		}
	}
	const int pages = CtrSpellbookPageCount();
	for (int page = 0; page < pages; ++page) {
		const Rectangle button = CompactTabRect(page);
		// Two 50% black blends leave roughly a quarter of the original stone
		// visible through the tab, without painting over the supplied frame.
		DrawHalfTransparentRectTo(out, button.position.x, button.position.y, button.size.width, button.size.height);
		DrawHalfTransparentRectTo(out, button.position.x, button.position.y, button.size.width, button.size.height);
		if (page == SpellbookTab)
			DrawHalfTransparentRectTo(out, button.position.x, button.position.y, button.size.width, button.size.height, PAL16_RED + 10);
		{
			DrawHorizontalLine(out, button.position, button.size.width, PAL16_BEIGE + 10);
			DrawVerticalLine(out, button.position, button.size.height, PAL16_BEIGE + 10);
			DrawHorizontalLine(out, button.position + Displacement { 0, button.size.height - 1 }, button.size.width, PAL16_BEIGE + 3);
			DrawVerticalLine(out, button.position + Displacement { button.size.width - 1, 0 }, button.size.height, PAL16_BEIGE + 3);
		}
		DrawString(out, std::to_string(page + 1), button, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhite });
	}
}
#endif

} // namespace

#ifdef __3DS__
int CtrSpellbookPageCount()
{
	return std::max(1, static_cast<int>((KnownCtrSpells().size() + CtrSpellRowsPerPage - 1) / CtrSpellRowsPerPage));
}

int CtrSpellbookRowCount()
{
	const int count = static_cast<int>(KnownCtrSpells().size());
	return std::clamp(count - SpellbookTab * CtrSpellRowsPerPage, 0, CtrSpellRowsPerPage);
}

bool CtrSpellBindingActive()
{
	return bindingOpen;
}

void MoveCtrSpellBinding(AxisDirection direction)
{
	const int current = CtrSpellBindingHit(CtrScreenToTop(MousePosition));
	if (direction.x == AxisDirectionX_NONE && direction.y == AxisDirectionY_NONE)
		return;
	int next = current < 0 ? 0 : current;
	if (current >= 0) {
		const Point origin = CtrSpellBindingPositions[current];
		int bestScore = 100000;
		for (int slot = 0; slot < CtrSpellShortcutCount; ++slot) {
			const Point p = CtrSpellBindingPositions[slot];
			const int dx = p.x - origin.x;
			const int dy = p.y - origin.y;
			if (direction.y != AxisDirectionY_NONE) {
				if ((direction.y == AxisDirectionY_UP && dy >= 0)
				    || (direction.y == AxisDirectionY_DOWN && dy <= 0))
					continue;
			} else if (dy != 0 || (direction.x == AxisDirectionX_LEFT && dx >= 0)
			    || (direction.x == AxisDirectionX_RIGHT && dx <= 0)) {
				continue;
			}
			const int score = dx * dx + dy * dy;
			if (score < bestScore) {
				bestScore = score;
				next = slot;
			}
		}
	}
	SetCursorPos(CtrTopToScreen(CtrSpellBindingRect(next).Center()));
}

int CtrSpellBindingSlot(ControllerButton button, bool modifierHeld)
{
	// Shoulder buttons are independent spell shortcuts, never modifiers.
	(void)modifierHeld;
	switch (button) {
	case ControllerButton_BUTTON_X: return 0;
	case ControllerButton_BUTTON_Y: return 1;
	case ControllerButton_BUTTON_LEFTSHOULDER: return 2;
	case ControllerButton_BUTTON_RIGHTSHOULDER: return 3;
	default: return -1;
	}
}

bool HandleCtrSpellBinding(ControllerButton button, bool modifierHeld)
{
	if (!SpellbookFlag || IsInspectingPlayer()) {
		bindingOpen = false;
		return false;
	}
	if (!bindingOpen) {
		if (button != ControllerButton_BUTTON_Y)
			return false;
		bindingSpell = FocusedCtrSpell();
		if (IsValidSpell(bindingSpell)) {
			bindingType = CtrSpellSource(bindingSpell);
			bindingOpen = true;
			SetCursorPos(CtrTopToScreen(CtrSpellBindingRect(0).Center()));
		}
		return true;
	}
	const int slot = button == ControllerButton_BUTTON_A
	    ? CtrSpellBindingHit(CtrScreenToTop(MousePosition))
	    : CtrSpellBindingSlot(button, modifierHeld);
	if (slot >= 0) {
		MyPlayer->_pSplHotKey[slot] = bindingSpell;
		MyPlayer->_pSplTHotKey[slot] = bindingType;
		if (slot == 0) {
			MyPlayer->_pRSpell = bindingSpell;
			MyPlayer->_pRSplType = bindingType;
		}
		// Existing per-character hotkey serialization persists these four slots.
		bindingOpen = false;
		RedrawEverything();
	} else if (button == ControllerButton_BUTTON_B || button == ControllerButton_BUTTON_BACK) {
		bindingOpen = false;
	}
	return true;
}

void DrawCtrHudSpellIcons(const Surface &out)
{
	static std::optional<OwnedSurface> icon;
	if (!icon) icon.emplace(37, 38);
	const Player &player = *MyPlayer;
	for (int slot = 0; slot < CtrSpellShortcutCount; ++slot) {
		const SpellID spell = slot == 0 ? player._pRSpell : player._pSplHotKey[slot];
		SpellType type = slot == 0 ? player._pRSplType : player._pSplTHotKey[slot];
		if (!IsValidSpell(spell)) continue;
		const uint64_t owned = type == SpellType::Skill ? player._pAblSpells
		    : type == SpellType::Spell ? player._pMemSpells
		    : type == SpellType::Charges ? player._pISpells
		    : type == SpellType::Scroll ? player._pScrlSpells : 0;
		if ((owned & GetSpellBitmask(spell)) == 0
		    || (type == SpellType::Spell && CheckSpell(player, spell, type, true) != SpellCheckResult::Success)
		    || (leveltype == DTYPE_TOWN && !GetSpellData(spell).isAllowedInTown()))
			type = SpellType::Invalid;
		FillRect(*icon, 0, 0, 37, 38, 0);
		SetSpellTrans(type);
		DrawSmallSpellIcon(*icon, { 0, 37 }, spell);
		const Rectangle target = CtrHudSpellSlot(slot);
		const Size size = CtrEnlargeImage({ 37, 38 }, target.size);
		out.ScaleBlitFrom(*icon, MakeSdlRect(0, 0, 37, 38),
		    MakeSdlRect(target.position.x + (target.size.width - size.width) / 2,
		        target.position.y, size.width, size.height));
	}
}

void DrawCtrSpellDescription(const Surface &out)
{
	const Rectangle box = CtrBottomInfoBox;
	FillRect(out, box.position.x, box.position.y, box.size.width, box.size.height, 0);
	const SpellID spell = bindingOpen ? bindingSpell : FocusedCtrSpell();
	const std::string text = WordWrapString(SpellDescription(spell), box.size.width - 12, GameFont12, 0);
	DrawString(out, text, box.inset({ 6, 6 }), { .flags = UiFlags::ColorWhite | UiFlags::AlignCenter | UiFlags::VerticalCenter, .spacing = 0 });
}
#endif

std::expected<void, std::string> InitSpellBook()
{
	ASSIGN_OR_RETURN(spellBookBackground, LoadCelWithStatus("data\\spellbk", static_cast<uint16_t>(SidePanelSize.width)));
	ASSIGN_OR_RETURN(spellBookButtons, LoadCelWithStatus("data\\spellbkb", SpellBookButtonWidth()));
	return LoadSmallSpellIcons();
}

void FreeSpellBook()
{
#ifdef __3DS__
	bindingOpen = false;
#endif
	FreeSmallSpellIcons();
	spellBookButtons = std::nullopt;
	spellBookBackground = std::nullopt;
}

void DrawSpellBook(const Surface &out)
{
#ifdef __3DS__
	DrawSpellBook3DS(out);
	return;
#endif
	constexpr int SpellBookButtonX = 7;
	constexpr int SpellBookButtonY = 348;
	ClxDraw(out, GetPanelPosition(UiPanels::Spell, { 0, 351 }), (*spellBookBackground)[0]);
	const int buttonX = gbIsHellfire && SpellbookTab < 5
	    ? SpellBookButtonWidthHellfire * SpellbookTab
	    : (SpellBookButtonWidthDiablo * SpellbookTab)
	        // BUGFIX: rendering of page 3 and page 4 buttons are both off-by-one pixel (fixed).
	        + (SpellbookTab == 2 || SpellbookTab == 3 ? 1 : 0);

	ClxDraw(out, GetPanelPosition(UiPanels::Spell, { SpellBookButtonX + buttonX, SpellBookButtonY }), (*spellBookButtons)[SpellbookTab]);
	const Player &player = *InspectPlayer;
	const uint64_t spl = player._pMemSpells | player._pISpells | player._pAblSpells;

	const int lineHeight = 18;

	int yp = 12;
	const int textPaddingTop = 7;
	for (size_t pageEntry = 0; pageEntry < SpellBookPageEntries; pageEntry++) {
		const SpellID sn = GetSpellFromSpellPage(SpellbookTab, pageEntry);
		if (IsValidSpell(sn) && (spl & GetSpellBitmask(sn)) != 0) {
			const SpellType st = GetSBookTrans(sn, true);
			SetSpellTrans(st);
			const Point spellCellPosition = GetPanelPosition(UiPanels::Spell, { 11, yp + SpellBookDescription.height });
			DrawSmallSpellIcon(out, spellCellPosition, sn);
			if (sn == player._pRSpell && st == player._pRSplType && !IsInspectingPlayer()) {
				SetSpellTrans(SpellType::Skill);
				DrawSmallSpellIconBorder(out, spellCellPosition);
			}

			const Point line0 { 0, yp + textPaddingTop };
			const Point line1 { 0, yp + textPaddingTop + lineHeight };
			PrintSBookStr(out, line0, pgettext("spell", GetSpellData(sn).sNameText));
			switch (GetSBookTrans(sn, false)) {
			case SpellType::Skill:
				PrintSBookStr(out, line1, _("Skill"));
				break;
			case SpellType::Charges: {
				const int charges = player.InvBody[INVLOC_HAND_LEFT]._iCharges;
				PrintSBookStr(out, line1, FormatRuntime(ngettext("Staff ({:d} charge)", "Staff ({:d} charges)", charges), charges));
			} break;
			default: {
				const int mana = GetManaAmount(player, sn) >> 6;
				const int lvl = player.GetSpellLevel(sn);
				PrintSBookStr(out, line0, FormatRuntime(pgettext(/* TRANSLATORS: UI constraints, keep short please.*/ "spellbook", "Level {:d}"), lvl), UiFlags::AlignRight);
				if (const StringOrView text = GetSpellPowerText(sn, lvl); !text.empty()) {
					PrintSBookStr(out, line1, text, UiFlags::AlignRight);
				}
				PrintSBookStr(out, line1, FormatRuntime(pgettext(/* TRANSLATORS: UI constraints, keep short please.*/ "spellbook", "Mana: {:d}"), mana));
			} break;
			}
		}
		yp += SpellBookDescription.height;
	}
}

void CheckSBook()
{
#ifdef __3DS__
	Point mousePos = CtrScreenToTop(MousePosition);
#else
	Point mousePos = MousePosition;
#endif

	// Icons are drawn in a column near the left side of the panel and aligned with the spell book description entries
	// Spell icons/buttons are 37x38 pixels, laid out from 11,18 with a 5 pixel margin between each icon. This is close
	// enough to the height of the space given to spell descriptions that we can reuse that value and subtract the
	// padding from the end of the area.
#ifdef __3DS__
	if (bindingOpen)
		return;
	const Rectangle iconArea { { 20, CtrSpellRowsY }, { 360, CtrSpellRowHeight * CtrSpellRowsPerPage } };
	constexpr int rowHeight = CtrSpellRowHeight;
#else
	const Rectangle iconArea = { GetPanelPosition(UiPanels::Spell, { 11, 18 }), Size { 37, (SpellBookDescription.height * 7) - 5 } };
	constexpr int rowHeight = SpellBookDescription.height;
#endif
	if (iconArea.contains(mousePos) && !IsInspectingPlayer()) {
		const SpellID sn = GetSpellFromSpellPage(SpellbookTab, (mousePos.y - iconArea.position.y) / rowHeight);
		Player &player = *InspectPlayer;
		const uint64_t spl = player._pMemSpells | player._pISpells | player._pAblSpells;
		if (IsValidSpell(sn) && (spl & GetSpellBitmask(sn)) != 0) {
			SpellType st = SpellType::Spell;
			if ((player._pISpells & GetSpellBitmask(sn)) != 0) {
				st = SpellType::Charges;
			}
			if ((player._pAblSpells & GetSpellBitmask(sn)) != 0) {
				st = SpellType::Skill;
			}
			player._pRSpell = sn;
			player._pRSplType = st;
#ifdef __3DS__
			player._pSplHotKey[0] = sn;
			player._pSplTHotKey[0] = st;
#endif
			RedrawEverything();
		}
		return;
	}

	// The width of the panel excluding the border is 305 pixels. This does not cleanly divide by 4 meaning Diablo tabs
	// end up with an extra pixel somewhere around the buttons. Vanilla Diablo had the buttons left-aligned, devilutionX
	// instead justifies the buttons and puts the gap between buttons 2/3. See DrawSpellBook
#ifdef __3DS__
	for (int page = 0; page < CtrSpellbookPageCount(); ++page) {
		if (CompactTabRect(page).contains(mousePos)) {
			SpellbookTab = page;
			break;
		}
	}
	return;
#else
	const int buttonWidth = SpellBookButtonWidth();
	// Tabs are drawn in a row near the bottom of the panel
	const Rectangle tabArea = { GetPanelPosition(UiPanels::Spell, { 7, 320 }), Size { 305, 29 } };
	if (tabArea.contains(mousePos)) {
		int hitColumn = mousePos.x - tabArea.position.x;
		// Clicking on the gutter currently activates tab 3. Could make it do nothing by checking for == here and return early.
		if (!gbIsHellfire && hitColumn > buttonWidth * 2) {
			// Subtract 1 pixel to account for the gutter between buttons 2/3
			hitColumn--;
		}
		SpellbookTab = hitColumn / buttonWidth;
	}
#endif
}

} // namespace devilution
