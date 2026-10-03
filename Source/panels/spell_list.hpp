#pragma once

#include <cstddef>
#include <vector>

#include "engine/point.hpp"
#ifdef __3DS__
#include "controls/axis_direction.h"
#endif
#include "engine/surface.hpp"
#include "tables/spelldat.h"

namespace devilution {

struct SpellListItem {
	Point location;
	SpellType type;
	SpellID id;
	bool isSelected;
	bool isVisible = true;
};

/**
 * @brief draws the current right mouse button spell.
 * @param out screen buffer representing the main UI panel
 */
void DrawSpell(const Surface &out);
void DrawSpellList(const Surface &out);
std::vector<SpellListItem> GetSpellListItems();
#ifdef __3DS__
bool Focus3dsSpellListItem(SpellID spell, SpellType type);
void Move3dsSpellListSelection(AxisDirection dir);
#endif
void SetSpell();
void SetSpeedSpell(size_t slot);
bool IsValidSpeedSpell(size_t slot);
void ToggleSpell(size_t slot);

/**
 * Draws the "Speed Book": the rows of known spells for quick-setting a spell that
 * show up when you click the spell slot at the control panel.
 */
void DoSpeedBook();

} // namespace devilution
