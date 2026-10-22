#include "game_mode.hpp"

#include <function_ref.hpp>

#include "options.h"
#ifdef __3DS__
#include "loadsave.h"
#include "levels/gendung.h"
#endif

namespace devilution {
namespace {
void OptionSharewareChanged()
{
	gbIsSpawn = *GetOptions().GameMode.shareware;
}
const auto OptionChangeHandlerShareware = (GetOptions().GameMode.shareware.SetValueChangedCallback(OptionSharewareChanged), true);
} // namespace

GameData sgGameInitInfo;
bool gbRunGame;
bool gbIsMultiplayer;
bool gbIsSpawn;
bool gbIsHellfire;
bool gbVanilla;
bool forceHellfire;
#ifdef __3DS__
void SetCtrHeroGameMode(bool hellfire)
{
	if (gbIsHellfire != hellfire)
		GetOptions().Mods.SetHellfireEnabled(hellfire);
	gbIsHellfireSaveGame = gbIsHellfire;
	giNumberOfLevels = gbIsHellfire ? 25 : 17;
}
#endif

} // namespace devilution
