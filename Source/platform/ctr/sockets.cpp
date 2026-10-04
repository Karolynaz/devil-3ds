#include "platform/ctr/sockets.hpp"

#include <cstdint>
#include <malloc.h>

#include <3ds.h>

#include "utils/log.hpp"

namespace devilution {

constexpr auto SOC_ALIGN = 0x1000;
constexpr auto SOC_BUFFERSIZE = 0x100000;
static u32 *socBuffer;
static bool initialized;
static LightLock socketLock = 1;
struct SocketGuard {
	SocketGuard() { LightLock_Lock(&socketLock); }
	~SocketGuard() { LightLock_Unlock(&socketLock); }
};

static bool waitForWifi()
{
	// 100 ms
	constexpr s64 sleepNano = 100 * 1000 * 1000;

	// 5 sec
	constexpr int loopCount = 5 * 1000 / 100;

	uint32_t wifi = 0;
	for (int i = 0; i < loopCount; ++i) {
		if (R_SUCCEEDED(ACU_GetWifiStatus(&wifi)) && wifi)
			return true;

		svcSleepThread(sleepNano);
	}

	return false;
}

void n3ds_socExit()
{
	SocketGuard guard;
	if (socBuffer == nullptr)
		return;

	socExit();
	free(socBuffer);
	socBuffer = nullptr;
	initialized = false;
}

bool n3ds_socInit()
{
	SocketGuard guard;
	if (initialized)
		return true;
	if (!waitForWifi()) {
		LogError("n3ds_socInit: Wifi off");
		return false;
	}

	socBuffer = (u32 *)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
	if (socBuffer == nullptr) {
		LogError("n3ds_socInit: memalign() failed");
		return false;
	}

	Result result = socInit(socBuffer, SOC_BUFFERSIZE);
	if (!R_SUCCEEDED(result)) {
		LogError("n3ds_socInit: socInit() failed");
		free(socBuffer);
		socBuffer = nullptr;
		return false;
	}

	initialized = true;
	return true;
}

} // namespace devilution
