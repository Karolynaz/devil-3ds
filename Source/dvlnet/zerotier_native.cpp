#include "dvlnet/zerotier_native.h"

#include <atomic>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>

#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#else
#include "utils/sdl2_backports.h"
#endif
#endif

#include <ankerl/unordered_dense.h>

#if defined(_WIN32) && !defined(DEVILUTIONX_WINDOWS_NO_WCHAR)
#include "utils/stdcompat/filesystem.hpp"
#ifdef DVL_HAS_FILESYSTEM
#define DVL_ZT_SYMLINK
#endif
#endif

#ifdef DVL_ZT_SYMLINK
#include <shlobj.h>
#ifdef PACKET_ENCRYPTION
#include <sodium.h>
#endif

#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"
#endif

#include <ZeroTierSockets.h>
#include <cstdlib>
#ifdef __3DS__
#include <3ds.h>
#include "platform/ctr/sockets.hpp"
#endif

#include "utils/algorithm/container.hpp"
#include "utils/log.hpp"
#include "utils/paths.h"

#include "dvlnet/zerotier_lwip.h"

namespace devilution {
namespace net {

namespace {

// static constexpr uint64_t zt_earth = 0x8056c2e21c000001;
constexpr uint64_t ZtNetwork = 0xa84ac5c10a7ebb5f;

#ifdef __3DS__
std::atomic<CtrZeroTierStartup> StartupState { CtrZeroTierStartup::Idle };
std::atomic_int StartupError { ZTS_ERR_OK };
std::atomic_int NetworkError { 0 };
LightLock PeerEventLock = 1;
struct PeerEventGuard {
	PeerEventGuard() { LightLock_Lock(&PeerEventLock); }
	~PeerEventGuard() { LightLock_Unlock(&PeerEventLock); }
};
#endif

std::atomic_bool zt_network_ready(false);
std::atomic_bool zt_node_online(false);
std::atomic_bool zt_joined(false);
std::atomic_uint zt_peers_ready(0);

ankerl::unordered_dense::map<uint64_t, zts_event_t> ztPeerEvents;

#ifdef DVL_ZT_SYMLINK
bool HasMultiByteChars(std::string_view path)
{
	return c_any_of(path, IsTrailUtf8CodeUnit);
}

#ifdef PACKET_ENCRYPTION
std::string ComputeAlternateFolderName(std::string_view path)
{
	const size_t hashSize = crypto_generichash_BYTES;
	unsigned char hash[hashSize];

	const int status = crypto_generichash(hash, hashSize,
	    reinterpret_cast<const unsigned char *>(path.data()), path.size(),
	    nullptr, 0);

	if (status != 0)
		return {};

	char buf[hashSize * 2];
	for (size_t i = 0; i < hashSize; ++i) {
		BufCopy(&buf[i * 2], AsHexPad2(hash[i]));
	}
	return std::string(buf, hashSize * 2);
}
#else
std::string ComputeAlternateFolderName(std::string_view path)
{
	return {};
}
#endif

std::string ToZTCompliantPath(std::string_view configPath)
{
	if (!HasMultiByteChars(configPath))
		return std::string(configPath);

	char commonAppDataPath[MAX_PATH];
	if (!SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_COMMON_APPDATA, NULL, 0, commonAppDataPath))) {
		LogVerbose("Failed to retrieve common application data path");
		return std::string(configPath);
	}

	std::error_code err;
	std::string alternateConfigPath = StrCat(commonAppDataPath, "\\diasurgical\\devilution");
	std::filesystem::create_directories(alternateConfigPath, err);
	if (err) {
		LogVerbose("Failed to create directories in ZT-compliant config path");
		return std::string(configPath);
	}

	std::string alternateFolderName = ComputeAlternateFolderName(configPath);
	if (alternateFolderName == "") {
		LogVerbose("Failed to hash config path for ZT");
		return std::string(configPath);
	}

	std::string symlinkPath = StrCat(alternateConfigPath, "\\", alternateFolderName);
	bool symlinkExists = std::filesystem::exists(
	    std::u8string_view(reinterpret_cast<const char8_t *>(symlinkPath.data()), symlinkPath.size()), err);
	if (err) {
		LogVerbose("Failed to determine if symlink for ZT-compliant config path exists");
		return std::string(configPath);
	}

	if (!symlinkExists) {
		std::filesystem::create_directory_symlink(
		    std::u8string_view(reinterpret_cast<const char8_t *>(configPath.data()), configPath.size()),
		    std::u8string_view(reinterpret_cast<const char8_t *>(symlinkPath.data()), symlinkPath.size()),
		    err);

		if (err) {
			LogVerbose("Failed to create symlink for ZT-compliant config path");
			return std::string(configPath);
		}
	}

	return StrCat(symlinkPath, "\\");
}
#endif

void Callback(void *ptr)
{
	auto *msg = reinterpret_cast<zts_event_msg_t *>(ptr);
#ifdef __3DS__
	Log("ZeroTier event: {}", msg->event_code);
#endif

	switch (msg->event_code) {
	case ZTS_EVENT_NODE_ONLINE:
		Log("ZeroTier: ZTS_EVENT_NODE_ONLINE, nodeId={:x}", (unsigned long long)msg->node->node_id);
		zt_node_online = true;
		if (!zt_joined) {
			const int result = zts_net_join(ZtNetwork);
			zt_joined = result == ZTS_ERR_OK;
#ifdef __3DS__
			if (!zt_joined) NetworkError = result;
#endif
		}
		break;

	case ZTS_EVENT_NODE_OFFLINE:
		Log("ZeroTier: ZTS_EVENT_NODE_OFFLINE");
		zt_node_online = false;
		break;

	case ZTS_EVENT_NETWORK_READY_IP6:
		Log("ZeroTier: ZTS_EVENT_NETWORK_READY_IP6, networkId={:x}", (unsigned long long)msg->network->net_id);
		zt_ip6setup();
		zt_network_ready = true;
#ifdef __3DS__
		NetworkError = 0;
#endif
		zt_peers_ready = SDL_GetTicks();
		break;

	case ZTS_EVENT_ADDR_ADDED_IP6:
		print_ip6_addr(&(msg->addr->addr));
		break;

#ifdef __3DS__
	case ZTS_EVENT_NETWORK_NOT_FOUND:
	case ZTS_EVENT_NETWORK_ACCESS_DENIED:
	case ZTS_EVENT_NETWORK_CLIENT_TOO_OLD:
	case ZTS_EVENT_NODE_FATAL_ERROR:
		NetworkError = msg->event_code;
		zt_network_ready = false;
		break;
	case ZTS_EVENT_NETWORK_DOWN:
		zt_network_ready = false;
		break;
#endif
	case ZTS_EVENT_PEER_DIRECT:
	case ZTS_EVENT_PEER_RELAY: {
#ifdef __3DS__
		PeerEventGuard guard;
#endif
		ztPeerEvents[msg->peer->peer_id] = static_cast<zts_event_t>(msg->event_code);
		if (!zerotier_peers_ready())
			zt_peers_ready = SDL_GetTicks();
		break;
	}
	case ZTS_EVENT_PEER_PATH_DEAD: {
#ifdef __3DS__
		PeerEventGuard guard;
#endif
		ztPeerEvents.erase(msg->peer->peer_id);
		break;
	}
	}
}

} // namespace

bool zerotier_network_ready()
{
	return zt_network_ready && zt_node_online;
}

bool zerotier_peers_ready()
{
	return SDL_GetTicks() - zt_peers_ready >= 5000;
}

#ifdef __3DS__
CtrZeroTierStartup zerotier_startup_state()
{
	return StartupState.load(std::memory_order_acquire);
}

bool zerotier_node_online() { return zt_node_online; }
int zerotier_network_error() { return NetworkError; }

int zerotier_startup_error()
{
	return StartupError.load(std::memory_order_relaxed);
}
#endif

void zerotier_network_start()
{
#ifdef __3DS__
	// Constructors run before the hero menu. Never perform network setup on
	// the UI thread while its palette is still black from the transition.
	const auto state = StartupState.load(std::memory_order_acquire);
	if (state == CtrZeroTierStartup::Starting || state == CtrZeroTierStartup::Started)
		return;
	StartupState.store(CtrZeroTierStartup::Starting, std::memory_order_release);
	StartupError.store(ZTS_ERR_OK, std::memory_order_relaxed);
	Thread worker = threadCreate([](void *) {
		auto finish = [](CtrZeroTierStartup state, int error = ZTS_ERR_OK) {
			StartupError.store(error, std::memory_order_relaxed);
			StartupState.store(state, std::memory_order_release);
		};
		try {
			if (!n3ds_socInit()) {
				finish(CtrZeroTierStartup::NoWifi);
				return;
			}
			const std::string path = paths::ConfigPath() + "zerotier";
			int result = zts_init_from_storage(path.c_str());
			if (result == ZTS_ERR_OK)
				result = zts_init_set_event_handler(&Callback);
			if (result == ZTS_ERR_OK)
				result = zts_node_start();
			finish(result == ZTS_ERR_OK ? CtrZeroTierStartup::Started : CtrZeroTierStartup::Failed, result);
		} catch (...) {
			finish(CtrZeroTierStartup::Failed, ZTS_ERR_GENERAL);
		}
	}, nullptr, 256 * 1024, 0x38, -2, true);
	if (worker == nullptr) {
		StartupError.store(ZTS_ERR_GENERAL, std::memory_order_relaxed);
		StartupState.store(CtrZeroTierStartup::Failed, std::memory_order_release);
	}
#else
	std::string configPath = paths::ConfigPath();
#ifdef DVL_ZT_SYMLINK
	configPath = ToZTCompliantPath(configPath);
#endif
	std::string ztpath = configPath + "zerotier";
	zts_init_from_storage(ztpath.c_str());
	zts_init_set_event_handler(&Callback);
	zts_node_start();
#endif
}

bool zerotier_is_relayed(uint64_t mac)
{
	bool isRelayed = true;
	if (zts_core_lock_obtain() != ZTS_ERR_OK)
		return isRelayed;
	zts_peer_info_t peerInfo;
	if (zts_core_query_peer_info(ZtNetwork, mac, &peerInfo) == ZTS_ERR_OK) {
#ifdef __3DS__
		PeerEventGuard guard;
#endif
		auto peerEvent = ztPeerEvents.find(peerInfo.peer_id);
		if (peerEvent != ztPeerEvents.end())
			isRelayed = (peerEvent->second == ZTS_EVENT_PEER_RELAY);
	}
	zts_core_lock_release();
	return isRelayed;
}

int zerotier_latency(uint64_t mac)
{
	int latency = -1;
	if (zts_core_lock_obtain() != ZTS_ERR_OK)
		return latency;
	zts_peer_info_t peerInfo;
	if (zts_core_query_peer_info(ZtNetwork, mac, &peerInfo) == ZTS_ERR_OK)
		latency = peerInfo.latency;
	zts_core_lock_release();
	return latency;
}

} // namespace net
} // namespace devilution
