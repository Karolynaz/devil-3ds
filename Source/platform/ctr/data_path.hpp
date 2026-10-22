#pragma once

#include "utils/file_util.h"

namespace devilution {

constexpr const char *CtrDefaultDataPath = "sdmc:/3ds/devil3ds/";
constexpr const char *CtrLegacyDataPath = "sdmc:/3ds/devilutionx/";

inline const char *ChooseCtrDataPath(const char *preferred, const char *legacy)
{
	// Keep data, configuration and saves together. Never create the new folder
	// before this check: that would hide an existing legacy installation.
	if (DirectoryExists(preferred) || !DirectoryExists(legacy))
		return preferred;
	return legacy;
}

inline const char *CtrDataPath()
{
	// Shared by SDL's base/pref paths; the SD card is checked only at startup.
	static const char *const selected = ChooseCtrDataPath(CtrDefaultDataPath, CtrLegacyDataPath);
	return selected;
}

} // namespace devilution
