#pragma once

#include "engine/surface.hpp"

namespace devilution {

enum class CtrPanelBackground {
	Character,
	Quest,
	Spells,
	InventoryWarrior,
	InventoryRogue,
	InventorySorcerer,
	Count,
};

void DrawCtrPanelBackground(const Surface &out, CtrPanelBackground background);

} // namespace devilution
