#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/diabloui.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "engine/point.hpp"
#include "DiabloUI/selok.h"
#include "engine/render/text_render.hpp"
#include "multi.h"
#include "storm/storm_net.hpp"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/paths.h"
#include "utils/ui_fwd.h"
#include "utils/utf8.hpp"
#ifdef __3DS__
#include "platform/ctr/sockets.hpp"
#ifndef DISABLE_ZERO_TIER
#include "dvlnet/zerotier_native.h"
#endif
#endif

namespace devilution {

int provider;
const char *ConnectionNames[] {
	"ZeroTier",
#ifdef __3DS__
	"TCP/IP (LAN)",
#else
	N_("Client-Server (TCP)"),
#endif
	N_("Offline"),
};

namespace {

char selconn_MaxPlayers[64];
char selconn_Description[256];
char selconn_Gateway[129];
bool selconn_ReturnValue = false;
bool selconn_EndMenu = false;
GameData *selconn_GameData;

std::vector<std::unique_ptr<UiListItem>> vecConnItems;
std::vector<std::unique_ptr<UiItemBase>> vecSelConnDlg;

#define DESCRIPTION_WIDTH 205

void SelconnEsc();
void SelconnFocus(size_t value);
void SelconnSelect(size_t value);

#if defined(__3DS__) && !defined(DISABLE_ZERO_TIER)
bool StartupCancelled;

bool WaitForZeroTierStartup()
{
	StartupCancelled = false;
	std::vector<std::unique_ptr<UiListItem>> actions;
	std::vector<std::unique_ptr<UiItemBase>> dialog;
	UiLoadBlackBackground();
	UiAddBackground(&dialog, false);
	dialog.push_back(std::make_unique<UiArtText>(_("Multiplayer").data(), MakeSdlRect(24, 24, 592, 36), UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver));
	char status[256] {};
	dialog.push_back(std::make_unique<UiArtText>(status, MakeSdlRect(50, 90, 540, 110), UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold, 1, 26));
	actions.push_back(std::make_unique<UiListItem>(_("Cancel"), 0));
	dialog.push_back(std::make_unique<UiList>(actions, 1, 160, 330, 320, 36, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));
	UiInitList(nullptr, [](size_t) { StartupCancelled = true; }, []() { StartupCancelled = true; }, dialog, true);

	net::zerotier_network_start();
	const uint32_t start = SDL_GetTicks();
	const std::unique_ptr<FILE, decltype(&std::fclose)> log(std::fopen((paths::ConfigPath() + "network-zerotier.log").c_str(), "wb"), std::fclose);
	uint32_t lastLog = start;
	int previousState = -1, previousOnline = -1, previousReady = -1, previousError = 0;
	net::CtrZeroTierStartup state = net::CtrZeroTierStartup::Starting;
	while (!StartupCancelled) {
		CopyUtf8(status, WordWrapString(net::zerotier_node_online() ? _("Joining ZeroTier network...") : _("Connecting to ZeroTier..."), 540, GameFont24, 1, CtrTextScale::TopScreen), sizeof(status));
		UiClearScreen();
		UiPollAndRender();
		// Identity generation and node/lwIP workers have lower priority than
		// this menu. Reserve CPU time for them on real hardware.
		SDL_Delay(50);
		state = net::zerotier_startup_state();
		const uint32_t elapsed = SDL_GetTicks() - start;
		const int online = net::zerotier_node_online();
		const int ready = net::zerotier_network_ready();
		const int error = net::zerotier_network_error();
		if (log && (static_cast<int>(state) != previousState || online != previousOnline || ready != previousReady
		    || error != previousError || SDL_GetTicks() - lastLog >= 5000)) {
			std::fprintf(log.get(), "%.3fs: startup %d; online %d; network ready %d; network error %d; startup error %d\n",
			    elapsed / 1000.0, static_cast<int>(state), online, ready, error, net::zerotier_startup_error());
			net::zerotier_log_diagnostics(log.get());
			std::fflush(log.get());
			lastLog = SDL_GetTicks();
			previousState = static_cast<int>(state); previousOnline = online; previousReady = ready; previousError = error;
		}
		if (net::zerotier_network_ready() || net::zerotier_network_error() != 0
		    || state == net::CtrZeroTierStartup::NoWifi || state == net::CtrZeroTierStartup::SecureRandomFailed || state == net::CtrZeroTierStartup::Failed
		    || elapsed >= 120000)
			break;
	}
	UiInitList_clear();
	ArtBackground = std::nullopt;
	if (StartupCancelled) {
		if (log) std::fprintf(log.get(), "Cancelled by user.\n");
		return false;
	}
	if (net::zerotier_network_ready())
		return true;
	std::string error;
	if (state == net::CtrZeroTierStartup::NoWifi || state == net::CtrZeroTierStartup::SecureRandomFailed)
		error = n3ds_networkError();
	else if (net::zerotier_network_error() != 0)
		error = FormatRuntime(_("ZeroTier network connection failed (error {})."), net::zerotier_network_error());
	else if (state == net::CtrZeroTierStartup::Starting || state == net::CtrZeroTierStartup::Started)
		error = net::zerotier_node_online()
		    ? _("ZeroTier is online, but the game network did not respond. Try again.")
		    : _("ZeroTier did not respond. Check internet access or try another network.");
	else
		error = FormatRuntime(_("Unable to start ZeroTier (error {})."), net::zerotier_startup_error());
	if (log) std::fprintf(log.get(), "%s\n", error.c_str());
	UiSelOkDialog(_("Multiplayer").data(), error.c_str(), false);
	return false;
}
#endif

void SelconnLoad()
{
#ifdef __3DS__
	UiLoadBlackBackground();
#else
	LoadBackgroundArt("ui_art\\selconn");
#endif

#ifndef NONET
#ifndef DISABLE_ZERO_TIER
	vecConnItems.push_back(std::make_unique<UiListItem>(std::string_view(ConnectionNames[SELCONN_ZT]), SELCONN_ZT));
#endif
#ifndef DISABLE_TCP
#ifdef __3DS__
	vecConnItems.push_back(std::make_unique<UiListItem>(std::string_view("TCP/IP (LAN)"), SELCONN_TCP));
#else
	vecConnItems.push_back(std::make_unique<UiListItem>(_(ConnectionNames[SELCONN_TCP]), SELCONN_TCP));
#endif
#endif
#endif
#ifndef __3DS__
	vecConnItems.push_back(std::make_unique<UiListItem>(_(ConnectionNames[SELCONN_LOOPBACK]), SELCONN_LOOPBACK));
#endif

	UiAddBackground(&vecSelConnDlg);
#ifndef __3DS__
	UiAddLogo(&vecSelConnDlg);
#endif

#ifndef __3DS__
	const Point uiPosition = GetUIRectangle().position;
#endif

#ifdef __3DS__
	vecSelConnDlg.push_back(std::make_unique<UiArtText>(_("Multiplayer").data(), MakeSdlRect(24, 24, 592, 36), UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver));
	vecSelConnDlg.push_back(std::make_unique<UiArtText>(selconn_Description, MakeSdlRect(50, 85, 540, 110), UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiSilver, 1, 26));
	vecSelConnDlg.push_back(std::make_unique<UiArtText>(_("Select Connection").data(), MakeSdlRect(50, 255, 540, 36), UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiSilver));
	vecSelConnDlg.push_back(std::make_unique<UiList>(vecConnItems, vecConnItems.size(), 65, 320, 510, 36, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::VerticalCenter | UiFlags::ColorUiGold));
#else
	const SDL_Rect rect1 = { (Sint16)(uiPosition.x + 24), ((Sint16)(uiPosition.y + 161)), 590, 35 };
	vecSelConnDlg.push_back(std::make_unique<UiArtText>(_("Multi Player Game").data(), rect1, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver, 3));

	const SDL_Rect rect2 = { (Sint16)(uiPosition.x + 35), (Sint16)(uiPosition.y + 218), DESCRIPTION_WIDTH, 21 };
	vecSelConnDlg.push_back(std::make_unique<UiArtText>(selconn_MaxPlayers, rect2, UiFlags::FontSize12 | UiFlags::ColorUiSilverDark));

	const SDL_Rect rect3 = { (Sint16)(uiPosition.x + 35), (Sint16)(uiPosition.y + 256), DESCRIPTION_WIDTH, 21 };
	vecSelConnDlg.push_back(std::make_unique<UiArtText>(_("Requirements:").data(), rect3, UiFlags::FontSize12 | UiFlags::ColorUiSilverDark));

	const SDL_Rect rect4 = { (Sint16)(uiPosition.x + 35), (Sint16)(uiPosition.y + 275), DESCRIPTION_WIDTH, 66 };
	vecSelConnDlg.push_back(std::make_unique<UiArtText>(selconn_Description, rect4, UiFlags::FontSize12 | UiFlags::ColorUiSilverDark, 1, 16));

	const SDL_Rect rect5 = { (Sint16)(uiPosition.x + 30), (Sint16)(uiPosition.y + 356), 220, 31 };
	vecSelConnDlg.push_back(std::make_unique<UiArtText>(_("no gateway needed").data(), rect5, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiSilver, 0));

	const SDL_Rect rect6 = { (Sint16)(uiPosition.x + 35), (Sint16)(uiPosition.y + 393), DESCRIPTION_WIDTH, 21 };
	vecSelConnDlg.push_back(std::make_unique<UiArtText>(selconn_Gateway, rect6, UiFlags::AlignCenter | UiFlags::FontSize12 | UiFlags::ColorUiSilverDark));

	const SDL_Rect rect7 = { (Sint16)(uiPosition.x + 300), (Sint16)(uiPosition.y + 211), 295, 33 };
	vecSelConnDlg.push_back(std::make_unique<UiArtText>(_("Select Connection").data(), rect7, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver, 3));

	const SDL_Rect rect8 = { (Sint16)(uiPosition.x + 16), (Sint16)(uiPosition.y + 427), 250, 35 };
	vecSelConnDlg.push_back(std::make_unique<UiArtTextButton>(_("Change Gateway"), nullptr, rect8, UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold | UiFlags::ElementHidden));

	vecSelConnDlg.push_back(std::make_unique<UiList>(vecConnItems, vecConnItems.size(), uiPosition.x + 305, (uiPosition.y + 256), 285, 26, UiFlags::AlignCenter | UiFlags::FontSize12 | UiFlags::VerticalCenter | UiFlags::ColorUiGoldDark));

	const SDL_Rect rect9 = { (Sint16)(uiPosition.x + 299), (Sint16)(uiPosition.y + 427), 140, 35 };
	vecSelConnDlg.push_back(std::make_unique<UiArtTextButton>(_("OK"), &UiFocusNavigationSelect, rect9, UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));

	const SDL_Rect rect10 = { (Sint16)(uiPosition.x + 454), (Sint16)(uiPosition.y + 427), 144, 35 };
	vecSelConnDlg.push_back(std::make_unique<UiArtTextButton>(_("Cancel"), &UiFocusNavigationEsc, rect10, UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));
#endif

	UiInitList(SelconnFocus, SelconnSelect, SelconnEsc, vecSelConnDlg, true);
}

void SelconnFree()
{
	ArtBackground = std::nullopt;

	vecConnItems.clear();

	vecSelConnDlg.clear();
}

void SelconnEsc()
{
	selconn_ReturnValue = false;
	selconn_EndMenu = true;
}

void SelconnFocus(size_t value)
{
	int players = MAX_PLRS;
	switch (vecConnItems[value]->m_value) {
	case SELCONN_TCP:
		CopyUtf8(selconn_Description, _("All computers must be connected to a TCP-compatible network."), sizeof(selconn_Description));
		players = MAX_PLRS;
		break;
	case SELCONN_ZT:
		CopyUtf8(selconn_Description, _("All computers must be connected to the internet."), sizeof(selconn_Description));
		players = MAX_PLRS;
		break;
	case SELCONN_LOOPBACK:
		CopyUtf8(selconn_Description, _("Play by yourself with no network exposure."), sizeof(selconn_Description));
		players = 1;
		break;
	}

	CopyUtf8(selconn_MaxPlayers, FormatRuntime(_("Players Supported: {:d}"), players), sizeof(selconn_MaxPlayers));
#ifdef __3DS__
	CopyUtf8(selconn_Description, WordWrapString(selconn_Description, 540, GameFont24, 1, CtrTextScale::TopScreen), sizeof(selconn_Description));
#else
	CopyUtf8(selconn_Description, WordWrapString(selconn_Description, DESCRIPTION_WIDTH), sizeof(selconn_Description));
#endif
}

void SelconnSelect(size_t value)
{
	provider = vecConnItems[value]->m_value;
#ifdef __3DS__
	if (provider == SELCONN_TCP && !n3ds_socInit()) {
		UiSelOkDialog(_("Multiplayer").data(), n3ds_networkError().c_str(), false);
		return;
	}
#endif

	SelconnFree();
#if defined(__3DS__) && !defined(DISABLE_ZERO_TIER)
	if (provider == SELCONN_ZT && !WaitForZeroTierStartup()) {
		SelconnLoad();
		return;
	}
#endif
	selconn_EndMenu = SNetInitializeProvider(provider, selconn_GameData);
	if (!selconn_EndMenu)
		SelconnLoad();
}

} // namespace

bool UiSelectProvider(GameData *gameData)
{

	selconn_GameData = gameData;
	SelconnLoad();

	selconn_ReturnValue = true;
	selconn_EndMenu = false;
	while (!selconn_EndMenu) {
		UiClearScreen();
		UiPollAndRender();
	}
	SelconnFree();

	return selconn_ReturnValue;
}

} // namespace devilution
