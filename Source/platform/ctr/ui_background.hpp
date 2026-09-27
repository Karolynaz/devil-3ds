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

enum class CtrBarArtwork {
	Health,
	Mana,
	Experience,
};

/** Draw CTR bar artwork with a fill fraction clamped to [0, 1]. */
void DrawCtrBarArtwork(const Surface &out, CtrBarArtwork bar, Point position, float fill);

} // namespace devilution
