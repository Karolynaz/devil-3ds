#include <3ds.h>
#include <cstdio>
#include <cstdlib>

#include "platform/ctr/cfgu_service.hpp"
#include "platform/ctr/random.hpp"
#include "platform/ctr/sockets.hpp"
#include "platform/ctr/system.h"

// ZeroTier sends multicast frames synchronously on the calling UI thread.
// Its nested send path exceeds libctru's default 32 KiB main stack.
// Match ZeroTier's 1 MiB minimum; the CIA exheader uses the same size.
extern "C" {
u32 __stacksize__ = 1024 * 1024;
}

using namespace devilution;

bool shouldDisableBacklight;

aptHookCookie cookie;

void aptHookFunc(APT_HookType hookType, void *param)
{
	switch (hookType) {
	case APTHOOK_ONSUSPEND:
		ctr_lcd_backlight_on();
		break;
	case APTHOOK_ONSLEEP:
		break;
	case APTHOOK_ONRESTORE:
		ctr_lcd_backlight_off();
		break;
	case APTHOOK_ONWAKEUP:
		ctr_lcd_backlight_off();
		break;
	case APTHOOK_ONEXIT:
		ctr_lcd_backlight_on();
		aptUnhook(&cookie);
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

	acInit();
	atexit([]() { acExit(); });

	// Multiplayer and the update checker initialize SOC when needed. Avoid a
	// five-second Wi-Fi wait and a 1 MiB socket buffer in single player.
	atexit([]() { n3ds_socExit(); });

#ifdef PACKET_ENCRYPTION
	randombytes_ctrrandom_init();
	atexit([]() {
		if (psGetSessionHandle())
			psExit();
	});
#endif
}
