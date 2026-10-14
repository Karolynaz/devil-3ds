#include "platform/ctr/zerotier_log.hpp"

#include <3ds.h>
#include <SDL.h>
#include <atomic>
#include <cstdio>
#include <string>
#include <sys/stat.h>

#include "dvlnet/zerotier_native.h"
#include "platform/ctr/network_trace_queue.hpp"
#include "platform/ctr/sockets.hpp"
#include "utils/paths.h"
#include "utils/log.hpp"

namespace devilution {
namespace {
CtrNetworkTraceQueue Queue;
LightLock QueueLock = 1;
Thread Writer = nullptr;
std::atomic_bool Stopping { false };
std::atomic_bool Enabled { false };
std::string LogPath;
uint32_t StartTime;

struct Guard {
	Guard() { LightLock_Lock(&QueueLock); }
	~Guard() { LightLock_Unlock(&QueueLock); }
};
void Enqueue(unsigned kind, int value, const char *stage)
{
	if (!Enabled.load(std::memory_order_acquire)) return;
	CtrNetworkTrace event { SDL_GetTicks() - StartTime, kind, value };
	if (stage != nullptr)
		std::snprintf(event.stage.data(), event.stage.size(), "%s", stage);
	Guard guard;
	Queue.push(event);
}
void Drain(FILE *file)
{
	CtrNetworkTrace event {};
	for (;;) {
		{
			Guard guard;
			if (!Queue.pop(event)) break;
		}
		if (file != nullptr) {
			if (event.stage[0] != '\0')
				std::fprintf(file, "%.3fs: %s; result %d\n", event.milliseconds / 1000.0, event.stage.data(), event.value);
			else
				std::fprintf(file, "%.3fs: trace kind %u; value %d (0x%08X)\n", event.milliseconds / 1000.0, event.kind, event.value, static_cast<unsigned>(event.value));
		}
	}
	uint32_t dropped;
	{
		Guard guard;
		dropped = Queue.takeDropped();
	}
	if (file != nullptr && dropped != 0)
		std::fprintf(file, "Trace queue overflow: %lu records dropped; transport counters remain available\n", static_cast<unsigned long>(dropped));
}
void WriteLog(void *)
{
	// Bound retained logs while preserving the preceding attempt.
	struct stat info {};
	if (stat(LogPath.c_str(), &info) == 0 && info.st_size > 4 * 1024 * 1024) {
		const std::string previous = LogPath + ".previous";
		std::remove(previous.c_str());
		std::rename(LogPath.c_str(), previous.c_str());
	}
	FILE *file = std::fopen(LogPath.c_str(), "ab");
	if (file == nullptr)
		LogError("Unable to open ZeroTier diagnostic log: {}", LogPath);
	if (file != nullptr) {
		bool newModel = false;
		APT_CheckNew3DS(&newModel);
		std::fprintf(file, "\n=== ZeroTier diagnostic session ZT-1; version %s; build %s; model %s ===\n", CTR_PORT_VERSION, CTR_BUILD_COMMIT, newModel ? "New 3DS" : "original 3DS");
		std::fprintf(file, "Trace kinds: 0 core phase; 1 SDK event; 2 UDP bind; 6 socket errno; 7 thread error; 10 select error; 14 RNG service result\nNo packet contents, private keys or identity files are recorded.\n");
	}
	uint32_t snapshotTime = SDL_GetTicks() - 5000;
	uint32_t flushTime = SDL_GetTicks();
	while (!Stopping.load(std::memory_order_acquire)) {
		Drain(file);
		const uint32_t now = SDL_GetTicks();
		if (file != nullptr && now - snapshotTime >= 5000) {
			snapshotTime = now;
			std::fprintf(file, "%.3fs: startup %d; online %d; ready %d; network error %d; startup error %d\n", (now - StartTime) / 1000.0,
			    static_cast<int>(net::zerotier_startup_state()), net::zerotier_node_online(), net::zerotier_network_ready(), net::zerotier_network_error(), net::zerotier_startup_error());
			net::zerotier_log_diagnostics(file);
		}
		if (file != nullptr && now - flushTime >= 1000) {
			flushTime = now;
			std::fflush(file);
		}
		svcSleepThread(100 * 1000 * 1000);
	}
	Drain(file);
	if (file != nullptr) {
		std::fprintf(file, "Diagnostic session ended\n");
		std::fclose(file);
	}
}
}
void CtrZeroTierLogStart()
{
	if (Writer == nullptr) {
		LogPath = paths::ConfigPath() + "network-zerotier.log";
		StartTime = SDL_GetTicks();
		Stopping.store(false);
		Enabled.store(true, std::memory_order_release);
		Writer = threadCreate(WriteLog, nullptr, 64 * 1024, 0x37, -2, false);
		if (Writer == nullptr) {
			Enabled.store(false);
			LogError("Unable to start ZeroTier diagnostic writer");
		}
	}
	CtrZeroTierLogStage("connection requested");
}
void CtrZeroTierLogStop()
{
	Enabled.store(false, std::memory_order_release);
	Stopping.store(true, std::memory_order_release);
	if (Writer != nullptr) {
		threadJoin(Writer, UINT64_MAX);
		threadFree(Writer);
		Writer = nullptr;
	}
}
void CtrZeroTierLogStage(const char *stage, int result) { Enqueue(100, result, stage); }
void CtrZeroTierLogTrace(unsigned kind, int value)
{
	if (kind == 0) {
		const char *stage = value == 1 ? "constructing ZeroTier core / identity"
		    : value == 2 ? "ZeroTier core constructed"
		    : value == 3 ? "refreshing physical UDP bindings"
		                 : value == 4 ? "physical UDP bindings refreshed" : "core phase";
		Enqueue(kind, value, stage);
	} else {
		Enqueue(kind, value, nullptr);
	}
}
}
