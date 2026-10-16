/**
 * @file gamemenu.cpp
 *
 * Implementation of the in-game menu functions.
 */
#include "gamemenu.h"

#ifdef __3DS__
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#endif

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#endif

#include "cursor.h"
#ifdef __3DS__
#include "control/control_chat.hpp"
#include "engine/render/text_render.hpp"
#include "platform/ctr/sockets.hpp"
#include "storm/storm_net.hpp"
#endif
#include "automap.h"
#include "diablo_msg.hpp"
#include "engine/backbuffer_state.hpp"
#include "engine/demomode.h"
#include "engine/events.hpp"
#include "engine/sound.h"
#include "engine/sound_defs.hpp"
#include "game_mode.hpp"
#include "gmenu.h"
#include "headless_mode.hpp"
#include "loadsave.h"
#include "multi.h"
#include "options.h"
#include "pfile.h"
#include "qol/floatingnumbers.h"
#include "utils/language.h"

#ifndef USE_SDL1
#include "controls/touch/renderers.h"
#endif

namespace devilution {

bool isGameMenuOpen = false;
#ifdef __3DS__
namespace { std::string MenuNetworkAddress; }
const char *gamemenu_network_address() { return MenuNetworkAddress.c_str(); }
#endif

namespace {

// Forward-declare menu handlers, used by the global menu structs below.
void GamemenuPrevious(bool bActivate);
void GamemenuNewGame(bool bActivate);
void GamemenuOptions(bool bActivate);
void GamemenuMusicVolume(bool bActivate);
void GamemenuSoundVolume(bool bActivate);
void GamemenuBrightness(bool bActivate);
void GamemenuSpeed(bool bActivate);
#ifdef __3DS__
void GamemenuSmallMap(bool bActivate);
void GamemenuMods(bool bActivate);
void GamemenuModsBack(bool bActivate);
void GamemenuModsOption(bool bActivate);
void GamemenuTalk(bool bActivate);
void GamemenuHostile(bool bActivate);
void GamemenuBuildModsMenu();
#endif

/** Contains the game menu items of the single player menu. */
TMenuItem sgSingleMenu[] = {
	// clang-format off
	// dwFlags,      pszStr,                  fnMenu
	{ GMENU_ENABLED, N_("Options"),           &GamemenuOptions    },
	{ GMENU_ENABLED, N_("Save Game"),         &gamemenu_save_game },
	{ GMENU_ENABLED, N_("Load Game"),         &gamemenu_load_game },
	{ GMENU_ENABLED, N_("Exit to Main Menu"), &GamemenuNewGame    },
#ifndef __3DS__
	{ GMENU_ENABLED, N_("Quit Game"),         &gamemenu_quit_game },
#endif
	{ GMENU_ENABLED, nullptr,                 nullptr             },
	// clang-format on
};
/** Contains the game menu items of the multi player menu. */
TMenuItem sgMultiMenu[] = {
	// clang-format off
	// dwFlags,      pszStr,                  fnMenu
	{ GMENU_ENABLED, N_("Options"),           &GamemenuOptions    },
#ifdef __3DS__
	{ GMENU_ENABLED, N_("Talk"),              &GamemenuTalk       },
	{ GMENU_ENABLED, N_("I'm hostile: No"),   &GamemenuHostile    },
#endif
	{ GMENU_ENABLED, N_("Exit to Main Menu"), &GamemenuNewGame    },
#ifndef __3DS__
	{ GMENU_ENABLED, N_("Quit Game"),         &gamemenu_quit_game },
#endif
	{ GMENU_ENABLED, nullptr,                 nullptr             },
	// clang-format on
};
TMenuItem sgOptionsMenu[] = {
	// clang-format off
	// dwFlags,                     pszStr,              fnMenu
	{ GMENU_ENABLED | GMENU_SLIDER, nullptr,             &GamemenuMusicVolume  },
	{ GMENU_ENABLED | GMENU_SLIDER, nullptr,             &GamemenuSoundVolume  },
	{ GMENU_ENABLED | GMENU_SLIDER, N_("Gamma"),         &GamemenuBrightness   },
	{ GMENU_ENABLED | GMENU_SLIDER, N_("Speed"),         &GamemenuSpeed        },
#ifdef __3DS__
	{ GMENU_ENABLED               , N_("Show small map"), &GamemenuSmallMap     },
	{ GMENU_ENABLED               , N_("Mods"),            &GamemenuMods         },
#endif
	{ GMENU_ENABLED               , N_("Previous Menu"), &GamemenuPrevious     },
	{ GMENU_ENABLED               , nullptr,             nullptr               },
	// clang-format on
};
#ifdef __3DS__
std::vector<TMenuItem> sgModsMenu;
std::vector<std::string> sgModsMenuLabels;
#endif
/** Specifies the menu names for music enabled and disabled. */
const char *const MusicToggleNames[] = {
	N_("Music"),
	N_("Music Disabled"),
};
/** Specifies the menu names for sound enabled and disabled. */
const char *const SoundToggleNames[] = {
	N_("Sound"),
	N_("Sound Disabled"),
};

void GamemenuUpdateSingle()
{
	sgSingleMenu[2].setEnabled(gbValidSaveFile);

	const bool enable = MyPlayer->_pmode != PM_DEATH && !MyPlayerIsDead;

	sgSingleMenu[0].setEnabled(enable);
}

void GamemenuPrevious(bool /*bActivate*/)
{
	gamemenu_on();
}

void GamemenuNewGame(bool /*bActivate*/)
{
	for (Player &player : Players) {
		player._pmode = PM_QUIT;
		player._pInvincible = true;
	}

	MyPlayerIsDead = false;
	if (!HeadlessMode) {
		RedrawEverything();
		scrollrt_draw_game_screen();
	}
	CornerStone.activated = false;
	gbRunGame = false;
	gamemenu_off();
}

void GamemenuSoundMusicToggle(const char *const *names, TMenuItem *menuItem, int volume)
{
	if (gbSndInited) {
		menuItem->addFlags(GMENU_ENABLED | GMENU_SLIDER);
		menuItem->pszStr = names[0];
		gmenu_slider_steps(menuItem, VOLUME_STEPS);
		gmenu_slider_set(menuItem, VOLUME_MIN, VOLUME_MAX, volume);
		return;
	}

	menuItem->removeFlags(GMENU_ENABLED | GMENU_SLIDER);
	menuItem->pszStr = names[1];
}

int GamemenuSliderMusicSound(TMenuItem *menuItem)
{
	return gmenu_slider_get(menuItem, VOLUME_MIN, VOLUME_MAX);
}

void GamemenuGetMusic()
{
	GamemenuSoundMusicToggle(MusicToggleNames, sgOptionsMenu, sound_get_or_set_music_volume(1));
}

void GamemenuGetSound()
{
	GamemenuSoundMusicToggle(SoundToggleNames, &sgOptionsMenu[1], sound_get_or_set_sound_volume(1));
}

void GamemenuGetBrightness()
{
	gmenu_slider_steps(&sgOptionsMenu[2], 21);
	gmenu_slider_set(&sgOptionsMenu[2], 0, 100, UpdateBrightness(-1));
}

void GamemenuGetSpeed()
{
	if (gbIsMultiplayer) {
		sgOptionsMenu[3].removeFlags(GMENU_ENABLED | GMENU_SLIDER);
		if (sgGameInitInfo.nTickRate >= 50)
			sgOptionsMenu[3].pszStr = _("Speed: Fastest").data();
		else if (sgGameInitInfo.nTickRate >= 40)
			sgOptionsMenu[3].pszStr = _("Speed: Faster").data();
		else if (sgGameInitInfo.nTickRate >= 30)
			sgOptionsMenu[3].pszStr = _("Speed: Fast").data();
		else if (sgGameInitInfo.nTickRate == 20)
			sgOptionsMenu[3].pszStr = _("Speed: Normal").data();
		return;
	}

	sgOptionsMenu[3].addFlags(GMENU_ENABLED | GMENU_SLIDER);

	sgOptionsMenu[3].pszStr = _("Speed").data();
	gmenu_slider_steps(&sgOptionsMenu[3], 46);
	gmenu_slider_set(&sgOptionsMenu[3], 20, 50, sgGameInitInfo.nTickRate);
}

int GamemenuSliderBrightness()
{
	return gmenu_slider_get(&sgOptionsMenu[2], 0, 100);
}

#ifdef __3DS__
void GamemenuUpdateMulti()
{
	sgMultiMenu[2].pszStr = MyPlayer->friendlyMode ? N_("I'm hostile: No") : N_("I'm hostile: Yes");
}

void GamemenuTalk(bool /*bActivate*/)
{
	gamemenu_off();
	CtrChatRequested = true;
}

void GamemenuHostile(bool /*bActivate*/)
{
	// The synchronized command handler changes the flag for all peers.
	NetSendCmd(true, CMD_FRIENDLYMODE);
}

void GamemenuSmallMapLabel()
{
	sgOptionsMenu[4].pszStr = *GetOptions().Gameplay.showSmallMap ? N_("Show small map: On") : N_("Show small map: Off");
}

void GamemenuSmallMap(bool /*bActivate*/)
{
	auto &option = GetOptions().Gameplay.showSmallMap;
	option.SetValue(!*option);
	SetAutomapType(*option ? AutomapType::Minimap : AutomapType::Opaque);
	UpdateMinimapRect();
	AutomapOffset = { 0, 0 };
	GamemenuSmallMapLabel();
	SaveOptions();
	RedrawEverything();
}

std::vector<OptionEntryBase *> GamemenuGetLiveModsOptions()
{
	GameplayOptions &gameplay = GetOptions().Gameplay;
	std::vector<OptionEntryBase *> options;
	for (OptionEntryBase *pEntry : gameplay.GetEntries()) {
		const OptionEntryFlags flags = pEntry->GetFlags();
		if (pEntry == &gameplay.tickRate || HasAnyOf(flags, OptionEntryFlags::Invisible | OptionEntryFlags::CantChangeInGame))
			continue;
		if (gbIsMultiplayer && HasAnyOf(flags, OptionEntryFlags::CantChangeInMultiPlayer))
			continue;
		if (pEntry == &gameplay.grabInput || pEntry == &gameplay.pauseOnFocusLoss)
			continue;
		// These values are copied into sgGameInitInfo when the session starts.
		if (pEntry == &gameplay.runInTown || pEntry == &gameplay.friendlyFire || pEntry == &gameplay.multiplayerFullQuests)
			continue;
		if (gbIsHellfire ? HasAnyOf(flags, OptionEntryFlags::OnlyDiablo) : HasAnyOf(flags, OptionEntryFlags::OnlyHellfire))
			continue;
		options.push_back(pEntry);
	}
	return options;
}

std::string GamemenuFormatModsLabel(std::string_view name, std::string_view value)
{
	const int availableWidth = gnScreenWidth - 2 * 56 - 48;
	std::string label(name);
	label.append(": ");
	label.append(value);
	if (GetLineWidth(label, GameFont12, 0, nullptr, CtrTextScale::TopScreen) <= availableWidth)
		return label;
	label.assign(name);
	label.push_back('\n');
	label.append(value);
	return label;
}

void GamemenuBuildModsMenu()
{
	struct MenuRow {
		std::string label;
		void (*handler)(bool);
		bool enabled;
		OptionEntryBase *option;
	};
	const std::vector<OptionEntryBase *> options = GamemenuGetLiveModsOptions();
	std::vector<MenuRow> rows;
	rows.reserve(options.size() + 1);
	for (OptionEntryBase *pOption : options) {
		std::string label = GamemenuFormatModsLabel(pOption->GetName(), pOption->GetValueDescription());
		rows.push_back({ std::move(label), &GamemenuModsOption, true, pOption });
	}
	rows.push_back({ std::string(_("Previous Menu")), &GamemenuModsBack, true, nullptr });

	sgModsMenuLabels.clear();
	sgModsMenuLabels.reserve(rows.size());
	sgModsMenu.clear();
	sgModsMenu.reserve(rows.size() + 1);
	for (MenuRow &row : rows)
		sgModsMenuLabels.push_back(std::move(row.label));
	for (size_t i = 0; i < rows.size(); i++) {
		const MenuRow &row = rows[i];
		const uint32_t flags = row.enabled ? GMENU_ENABLED : 0;
		sgModsMenu.push_back({ flags, sgModsMenuLabels[i].c_str(), row.handler, row.option, false });
	}
	sgModsMenu.push_back({ GMENU_ENABLED, nullptr, nullptr });
}

void GamemenuMods(bool /*bActivate*/)
{
	GamemenuBuildModsMenu();
	gmenu_set_items(sgModsMenu.data(), nullptr);
}

void GamemenuModsBack(bool /*bActivate*/)
{
	gmenu_set_items(sgOptionsMenu, nullptr);
}

void GamemenuModsOption(bool /*bActivate*/)
{
	if (sgpActivatedMenuItem == nullptr || sgpActivatedMenuItem->userData == nullptr)
		return;
	auto *pOption = static_cast<OptionEntryBase *>(sgpActivatedMenuItem->userData);
	if (pOption->GetType() == OptionEntryType::Boolean) {
		auto *pBooleanOption = static_cast<OptionEntryBoolean *>(pOption);
		pBooleanOption->SetValue(!**pBooleanOption);
	} else if (pOption->GetType() == OptionEntryType::List) {
		auto *pListOption = static_cast<OptionEntryListBase *>(pOption);
		const size_t listSize = pListOption->GetListSize();
		if (listSize != 0)
			pListOption->SetActiveListIndex((pListOption->GetActiveListIndex() + 1) % listSize);
	}
	const size_t itemIndex = static_cast<size_t>(sgpActivatedMenuItem - sgModsMenu.data());
	std::string label = GamemenuFormatModsLabel(pOption->GetName(), pOption->GetValueDescription());
	sgModsMenuLabels[itemIndex] = std::move(label);
	sgpActivatedMenuItem->pszStr = sgModsMenuLabels[itemIndex].c_str();
}

#endif

void GamemenuOptions(bool /*bActivate*/)
{
#ifdef __3DS__
	GamemenuSmallMapLabel();
#endif
	GamemenuGetMusic();
	GamemenuGetSound();
	GamemenuGetBrightness();
	GamemenuGetSpeed();
	gmenu_set_items(sgOptionsMenu, nullptr);
}

void GamemenuMusicVolume(bool bActivate)
{
	if (bActivate) {
		if (gbMusicOn) {
			gbMusicOn = false;
			music_stop();
			sound_get_or_set_music_volume(VOLUME_MIN);
		} else {
			gbMusicOn = true;
			sound_get_or_set_music_volume(VOLUME_MAX);
			music_start(GetLevelMusic(leveltype));
		}
	} else {
		const int volume = GamemenuSliderMusicSound(&sgOptionsMenu[0]);
		sound_get_or_set_music_volume(volume);
		if (volume == VOLUME_MIN) {
			if (gbMusicOn) {
				gbMusicOn = false;
				music_stop();
			}
		} else if (!gbMusicOn) {
			gbMusicOn = true;
			music_start(GetLevelMusic(leveltype));
		}
	}

	GamemenuGetMusic();
}

void GamemenuSoundVolume(bool bActivate)
{
	if (bActivate) {
		if (gbSoundOn) {
			gbSoundOn = false;
			sound_stop();
			sound_get_or_set_sound_volume(VOLUME_MIN);
		} else {
			gbSoundOn = true;
			sound_get_or_set_sound_volume(VOLUME_MAX);
		}
	} else {
		const int volume = GamemenuSliderMusicSound(&sgOptionsMenu[1]);
		sound_get_or_set_sound_volume(volume);
		if (volume == VOLUME_MIN) {
			if (gbSoundOn) {
				gbSoundOn = false;
				sound_stop();
			}
		} else if (!gbSoundOn) {
			gbSoundOn = true;
		}
	}
	PlaySFX(SfxID::MenuMove);
	GamemenuGetSound();
}

void GamemenuBrightness(bool bActivate)
{
	int brightness;
	if (bActivate) {
		brightness = UpdateBrightness(-1);
		brightness = (brightness == 0) ? 100 : 0;
	} else {
		brightness = GamemenuSliderBrightness();
	}

	UpdateBrightness(brightness);
	GamemenuGetBrightness();
}

void GamemenuSpeed(bool bActivate)
{
	if (bActivate) {
		if (sgGameInitInfo.nTickRate != 20)
			sgGameInitInfo.nTickRate = 20;
		else
			sgGameInitInfo.nTickRate = 50;
		gmenu_slider_set(&sgOptionsMenu[3], 20, 50, sgGameInitInfo.nTickRate);
	} else {
		sgGameInitInfo.nTickRate = gmenu_slider_get(&sgOptionsMenu[3], 20, 50);
	}

	GetOptions().Gameplay.tickRate.SetValue(sgGameInitInfo.nTickRate);
	gnTickDelay = 1000 / sgGameInitInfo.nTickRate;
}

} // namespace

void gamemenu_exit_game(bool bActivate)
{
	GamemenuNewGame(bActivate);
}

void gamemenu_quit_game(bool bActivate)
{
	GamemenuNewGame(bActivate);
#ifndef NOEXIT
	gbRunGameResult = false;
#else
	ReturnToMainMenu = true;
#endif
}

void gamemenu_load_game(bool /*bActivate*/)
{
	EventHandler saveProc = SetEventHandler(DisableInputEventHandler);
	gamemenu_off();
	ClearFloatingNumbers();
	NewCursor(CURSOR_NONE);
	InitDiabloMsg(EMSG_LOADING);
	RedrawEverything();
	DrawAndBlit();

	const std::array<SDL_Color, 256> prevPalette = logical_palette;
#ifndef USE_SDL1
	DeactivateVirtualGamepad();
	FreeVirtualGamepadTextures();
#endif
	if (std::expected<void, std::string> result = LoadGame(false); !result.has_value()) {
		app_fatal(result.error());
	}
#if !defined(USE_SDL1) && !defined(__vita__)
	if (renderer != nullptr) {
		InitVirtualGamepadTextures(*renderer);
	}
#endif
	ClrDiabloMsg();
	PaletteFadeOut(8, prevPalette);

	LoadPWaterPalette();
	NewCursor(CURSOR_HAND);
	CornerStone.activated = false;
	MyPlayerIsDead = false;
	RedrawEverything();
	DrawAndBlit();
	PaletteFadeIn(8);
	NewCursor(CURSOR_HAND);
	interface_msg_pump();
	SetEventHandler(saveProc);
}

void gamemenu_save_game(bool /*bActivate*/)
{
	if (pcurs != CURSOR_HAND) {
		return;
	}

	if (MyPlayer->_pmode == PM_DEATH || MyPlayerIsDead) {
		gamemenu_off();
		return;
	}

	EventHandler saveProc = SetEventHandler(DisableInputEventHandler);
	NewCursor(CURSOR_NONE);
	gamemenu_off();
	InitDiabloMsg(EMSG_SAVING);
	RedrawEverything();
	DrawAndBlit();
	SaveGame();
	ClrDiabloMsg();
	InitDiabloMsg(EMSG_GAME_SAVED, 2500);
	RedrawEverything();
	NewCursor(CURSOR_HAND);
	if (CornerStone.activated) {
		CornerstoneSave();
		if (!demo::IsRunning()) SaveOptions();
	}
	interface_msg_pump();
	SetEventHandler(saveProc);
}

void gamemenu_on()
{
	isGameMenuOpen = true;
#ifdef __3DS__
	MenuNetworkAddress.clear();
	if (gbIsMultiplayer && provider == SELCONN_TCP) {
		const std::string ip = n3ds_localAddress();
		MenuNetworkAddress = ip.empty() ? "IP: unavailable" : "IP: " + ip;
	} else if (gbIsMultiplayer && provider == SELCONN_ZT && !GameName.empty()) {
		MenuNetworkAddress = (!PublicGame ? "Game ID: " : "Game: ") + GameName;
	}
#endif
	if (!gbIsMultiplayer) {
		gmenu_set_items(sgSingleMenu, GamemenuUpdateSingle);
	} else {
		#ifdef __3DS__
		GamemenuUpdateMulti();
		gmenu_set_items(sgMultiMenu, GamemenuUpdateMulti);
#else
		gmenu_set_items(sgMultiMenu, nullptr);
#endif
	}
	PressEscKey();
}

void gamemenu_off()
{
	isGameMenuOpen = false;
	gmenu_set_items(nullptr, nullptr);
}

void gamemenu_handle_previous()
{
	if (gmenu_is_active())
		gamemenu_off();
	else
		gamemenu_on();
}

} // namespace devilution
