#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <SDL.h>
#include "controls/game_controls.h"
#include "platform/ctr/display.hpp"
#include "nthread.h"
#include "diablo.h"
#include "game_mode.hpp"
#include "utils/log.hpp"

#include "platform/ctr/cfgu_service.hpp"
#include "platform/ctr/random.hpp"
#include "platform/ctr/sockets.hpp"
#include "platform/ctr/system.h"
#include "platform/ctr/update.hpp"
#include "multi.h"
#include "storm/storm_net.hpp"
#if !defined(NONET) && !defined(DISABLE_ZERO_TIER)
#include "dvlnet/zerotier_native.h"
#include "platform/ctr/zerotier_log.hpp"
#endif

// ZeroTier sends multicast frames synchronously on the calling UI thread.
// Its nested send path exceeds libctru's default 32 KiB main stack.
// Match ZeroTier's 1 MiB minimum; the CIA exheader uses the same size.
extern "C" {
u32 __stacksize__ = 1024 * 1024;
}

using namespace devilution;

bool shouldDisableBacklight;

aptHookCookie cookie;
namespace {
std::atomic_bool ResumePending { false };
std::atomic_uint ResumeCount { 0 };
}

void ctr_process_resume()
{
	if (!ResumePending.exchange(false, std::memory_order_acq_rel)) return;
	// Run only after SDL's aptMainLoop has returned: APT restore hooks run
	// before DSP wake-up completes. Never stop/reopen audio or join workers
	// from a hook while the SDK is restoring those same services.
	devilution::Reset3DSInputAfterResume();
	devilution::CTR_InvalidateFramePresenter();
	if (devilution::gbRunGame && !devilution::gbIsMultiplayer)
		devilution::last_tick = SDL_GetTicks();
	devilution::LogInfo("3DS wake/restore {}: input and frame state restored", ResumeCount.load());
}

void aptHookFunc(APT_HookType hookType, void *param)
{
	switch (hookType) {
	case APTHOOK_ONSUSPEND:
		ctr_lcd_backlight_on();
		break;
	case APTHOOK_ONSLEEP:
		break;
	case APTHOOK_ONRESTORE:
		ResumeCount.fetch_add(1, std::memory_order_relaxed);
		ResumePending.store(true, std::memory_order_release);
		ctr_lcd_backlight_off();
		break;
	case APTHOOK_ONWAKEUP:
		ResumeCount.fetch_add(1, std::memory_order_relaxed);
		ResumePending.store(true, std::memory_order_release);
		ctr_lcd_backlight_off();
		break;
	case APTHOOK_ONEXIT:
		ctr_lcd_backlight_on();
		break;
	default:
		break;
	}
}

void ctr_lcd_backlight_on()
{
	if (!shouldDisableBacklight)
		return;
	gspLcdInit();
	GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTTOM);
	gspLcdExit();
}

void ctr_lcd_backlight_off()
{
	if (!shouldDisableBacklight)
		return;
	gspLcdInit();
	GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
	gspLcdExit();
}

bool ctr_check_dsp()
{
	FILE *dsp = fopen("sdmc:/3ds/dspfirm.cdc", "r");
	if (dsp != NULL) {
		fclose(dsp);
	}
	return true;
}

bool ctr_is_n3ds()
{
	bool isN3DS;
	Result res = APT_CheckNew3DS(&isN3DS);
	return R_SUCCEEDED(res) && isN3DS;
}

bool ctr_should_disable_backlight()
{
	// Keep bottom screen active and lit for dual-screen HUD and menus
	return false;
}

void ctr_sys_shutdown()
{
	static bool stopped = false;
	if (stopped)
		return;
	stopped = true;
	NetClose();
	SNetDestroy();
	CtrStopUpdateCheck();
#if !defined(NONET) && !defined(DISABLE_ZERO_TIER)
	net::zerotier_network_shutdown();
	CtrZeroTierLogStop();
#endif
}

void ctr_sys_init()
{
	if (ctr_check_dsp() == false)
		exit(0);

	aptHook(&cookie, aptHookFunc, NULL);
	atexit([]() { aptUnhook(&cookie); });

	if (ctr_is_n3ds())
		osSetSpeedupEnable(true);

	shouldDisableBacklight = ctr_should_disable_backlight();

	ctr_lcd_backlight_off();
	atexit([]() { ctr_lcd_backlight_on(); });

	romfsInit();
	atexit([]() { romfsExit(); });

	// Multiplayer and the update checker initialize SOC when needed. Avoid a
	// five-second Wi-Fi wait and a 1 MiB socket buffer in single player.
	atexit([]() { n3ds_socExit(); });

#ifdef PACKET_ENCRYPTION
	randombytes_ctrrandom_init();
#endif
	// ZeroTier also uses ps:ps when packet encryption is disabled.
	atexit([]() {
		if (psGetSessionHandle())
			psExit();
	});
	// Runs before SOC/RomFS exit and global destructors, including early exits.
	atexit(ctr_sys_shutdown);
}
