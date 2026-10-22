#pragma once

#include <algorithm>
#include <cstddef>
#include "engine/rectangle.hpp"

namespace devilution {
constexpr int CtrSpellIconSize = 56;
constexpr int CtrSpellColumns = 6;
constexpr int CtrSpellColumnPitch = 60;
constexpr int CtrSpellRowPitch = 58;
constexpr int CtrSpellRowWidth = (CtrSpellColumns - 1) * CtrSpellColumnPitch + CtrSpellIconSize;
constexpr Rectangle CtrSpellRect(int column, int row, int rows)
{
	return { { (400 - CtrSpellRowWidth) / 2 + column * CtrSpellColumnPitch,
	             (240 - ((rows - 1) * CtrSpellRowPitch + CtrSpellIconSize)) / 2 + row * CtrSpellRowPitch },
		{ CtrSpellIconSize, CtrSpellIconSize } };
}
constexpr size_t CtrSpellScrollFirst(size_t selected, size_t first, size_t count)
{
	if (count <= CtrSpellColumns) return 0;
	first = std::min(first, count - CtrSpellColumns);
	if (selected < first) return selected;
	if (selected >= first + CtrSpellColumns) return selected - CtrSpellColumns + 1;
	return first;
}
} // namespace devilution
