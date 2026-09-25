#pragma once

#include "engine/surface.hpp"

namespace devilution {

enum class CtrPanelBackground {
	Stone,
	Quest,
};

void DrawCtrPanelBackground(const Surface &out, CtrPanelBackground background);

} // namespace devilution
