#include "platform/ctr/sockets.hpp"

#include <cstdint>
#include <cstdio>
#include <malloc.h>

#include <3ds.h>

#include "utils/log.hpp"

namespace devilution {

constexpr auto SOC_ALIGN = 0x1000;
constexpr auto SOC_BUFFERSIZE = 0x100000;
static u32 *socBuffer;
static bool initialized;
static bool acInitialized;
static bool ndmInitialized;
static bool infrastructureReserved;
static std::string networkError;
static LightLock socketLock = 1;
struct SocketGuard {
	SocketGuard() { LightLock_Lock(&socketLock); }
	~SocketGuard() { LightLock_Unlock(&socketLock); }
};

static bool serviceFailed(const char *service, Result result)
{
	char error[128];
	std::snprintf(error, sizeof(error), "%s failed (0x%08lX).", service, static_cast<unsigned long>(static_cast<u32>(result)));
	networkError = error;
	LogError("3DS network: {}", networkError);
	return false;
}

static void releaseConnectionServices()
{
	if (infrastructureReserved) NDMU_LeaveExclusiveState();
	infrastructureReserved = false;
	if (ndmInitialized) ndmuExit();
	ndmInitialized = false;
	if (acInitialized) acExit();
	acInitialized = false;
}

static bool waitForWifi()
{
	constexpr s64 sleepNano = 100 * 1000 * 1000;
	constexpr int loopCount = 5 * 1000 / 100;
	for (int i = 0; i < loopCount; ++i) {
		u32 wifi = 0;
		const Result result = ACU_GetWifiStatus(&wifi);
		if (R_FAILED(result)) return serviceFailed("ac:u Wi-Fi status", result);
		if (wifi != 0) return true;
		svcSleepThread(sleepNano);
	}
	networkError = "No Wi-Fi connection. Connect in System Settings.";
	return false;
}

void n3ds_socExit()
{
	SocketGuard guard;
	if (initialized) socExit();
	free(socBuffer);
	socBuffer = nullptr;
	initialized = false;
	// The application joins all network workers before releasing these services.
	releaseConnectionServices();
}

bool n3ds_socInit()
{
	SocketGuard guard;
	networkError.clear();
	if (!acInitialized) {
		const Result result = acInit();
		if (R_FAILED(result)) return serviceFailed("ac:u initialization", result);
		acInitialized = true;
	}
	// Keep infrastructure Wi-Fi active while SOC/ZeroTier are in use, instead
	// of letting the network daemon switch back to background communications.
	if (!ndmInitialized) {
		const Result result = ndmuInit();
		if (R_FAILED(result)) {
			releaseConnectionServices();
			return serviceFailed("ndm:u initialization", result);
		}
		ndmInitialized = true;
	}
	if (!infrastructureReserved) {
		const Result result = NDMU_EnterExclusiveState(NDM_EXCLUSIVE_STATE_INFRASTRUCTURE);
		if (R_FAILED(result)) {
			releaseConnectionServices();
			return serviceFailed("ndm:u internet mode", result);
		}
		infrastructureReserved = true;
	}
	// Recheck Wi-Fi on retries too: an existing SOC buffer is not proof that
	// the console stayed connected after HOME, sleep or a lost access point.
	if (!waitForWifi()) {
		if (!initialized) releaseConnectionServices();
		return false;
	}
	if (initialized) return true;
	socBuffer = static_cast<u32 *>(memalign(SOC_ALIGN, SOC_BUFFERSIZE));
	if (socBuffer == nullptr) {
		networkError = "Not enough memory for networking.";
		releaseConnectionServices();
		return false;
	}
	const Result result = socInit(socBuffer, SOC_BUFFERSIZE);
	if (R_FAILED(result)) {
		free(socBuffer);
		socBuffer = nullptr;
		releaseConnectionServices();
		return serviceFailed("soc:U initialization", result);
	}
	initialized = true;
	return true;
}

bool n3ds_initSecureRandom()
{
	SocketGuard guard;
	if (!psGetSessionHandle()) {
		const Result result = psInit();
		if (R_FAILED(result)) return serviceFailed("ps:ps initialization", result);
	}
	unsigned char probe[32];
	const Result result = PS_GenerateRandomBytes(probe, sizeof(probe));
	if (R_FAILED(result)) return serviceFailed("ps:ps random generation", result);
	return true;
}

std::string n3ds_networkError()
{
	SocketGuard guard;
	return networkError;
}

} // namespace devilution
