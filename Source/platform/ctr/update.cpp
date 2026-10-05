#include "platform/ctr/update.hpp"
#include "platform/ctr/update_json.hpp"
#include "platform/ctr/sockets.hpp"
#include <3ds.h>
#include <curl/curl.h>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>

namespace devilution {
namespace {
std::atomic_bool Ready { false };
Thread Worker = nullptr;
CtrUpdateResult Result { CtrUpdateState::Error, {}, {} };
constexpr size_t MaxResponse = 512 * 1024;

size_t Receive(char *data, size_t size, size_t count, void *user)
{
	auto &response = *static_cast<std::string *>(user);
	if (response.size() > MaxResponse || (size != 0 && count > (MaxResponse - response.size()) / size)) return 0;
#ifdef __cpp_exceptions
	try {
#endif
		response.append(data, size * count);
#ifdef __cpp_exceptions
	} catch (const std::bad_alloc &) {
		// Exceptions must not cross libcurl's C callback boundary.
		return 0;
	}
#endif
	return size * count;
}

bool Get(std::string_view url, std::string &body, std::string &error)
{
	const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
	CURL *curl = handle.get();
	if (!curl) { error = "HTTP initialization failed."; return false; }
	char errorBuffer[CURL_ERROR_SIZE] {};
	const std::string address { url };
	curl_easy_setopt(curl, CURLOPT_URL, address.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "devil-3ds/" CTR_PORT_VERSION);
	// SOC is IPv4-only; portlibs supports TLS 1.2 and HTTP/1.1.
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2 | CURL_SSLVERSION_MAX_TLSv1_2);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	curl_easy_setopt(curl, CURLOPT_CAINFO, "romfs:/cacert.pem");
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Receive);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	const CURLcode code = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	if (code != CURLE_OK) { error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code); return false; }
	if (status != 200) { error = status == 403 || status == 429 ? "GitHub rate limit. Try again later." : "GitHub HTTP error: " + std::to_string(status); return false; }
	return true;
}

void Check(void *)
{
	CtrUpdateResult result { CtrUpdateState::Error, {}, {} };
#ifdef __cpp_exceptions
	try {
#endif
		if (!n3ds_socInit()) {
			result.detail = "No Wi-Fi connection. Connect in System Settings.";
		} else {
			// The portlibs mbedTLS entropy source calls sslcGenerateRandomData.
			// It requires ssl:C to be initialized before TLS seeding.
			struct SslService {
				bool initialized = R_SUCCEEDED(sslcInit(0));
				~SslService() { if (initialized) sslcExit(); }
			} ssl;
			unsigned char randomProbe[32];
			if (!ssl.initialized || R_FAILED(sslcGenerateRandomData(randomProbe, sizeof(randomProbe)))) {
				result.detail = "Unable to initialize secure network service.";
			} else if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
				result.detail = "HTTP initialization failed.";
			} else {
				struct CurlGlobalCleanup {
					~CurlGlobalCleanup() { curl_global_cleanup(); }
				} cleanup;
				std::string body;
				if (Get("https://api.github.com/repos/Karolynaz/devil-3ds/releases/latest", body, result.detail)) {
					const auto tag = CtrJsonString(body, "tag_name");
					if (!tag || !CtrValidReleaseTag(*tag)) {
						result.detail = "Invalid release response from GitHub.";
					} else {
						result.version = *tag;
						body.clear();
						const std::string url = std::string("https://api.github.com/repos/Karolynaz/devil-3ds/compare/") + CTR_BUILD_COMMIT + "..." + *tag + "?per_page=1";
						if (Get(url, body, result.detail)) {
							const auto status = CtrJsonString(body, "status");
							if (status && *status == "ahead") result.state = CtrUpdateState::Available;
							else if (status && (*status == "identical" || *status == "behind")) result.state = CtrUpdateState::Current;
							else if (status && *status == "diverged") result.state = CtrUpdateState::Different;
							else result.detail = "Invalid version comparison from GitHub.";
						}
					}
				}
			}
		}
#ifdef __cpp_exceptions
	} catch (const std::bad_alloc &) {
		result.state = CtrUpdateState::Error;
		result.version.clear();
		result.detail = "Out of memory.";
	} catch (...) {
		result.state = CtrUpdateState::Error;
		result.version.clear();
		result.detail = "Check failed.";
	}
#endif
	Result = std::move(result);
	Ready.store(true, std::memory_order_release);
}

void Stop()
{
	if (!Worker) return;
	threadJoin(Worker, UINT64_MAX);
	threadFree(Worker);
	Worker = nullptr;
}
} // namespace

std::string_view CtrBuildCommit() { return CTR_BUILD_COMMIT; }
std::string_view CtrPortVersion() { return CTR_PORT_VERSION; }
bool CtrUpdateBusy() { return Worker && !Ready.load(std::memory_order_acquire); }

void CtrCheckForUpdates()
{
	if (CtrUpdateBusy()) return;
	Stop();
	Ready = false;
	static const bool registered = []() { std::atexit(Stop); return true; }();
	(void)registered;
	Worker = threadCreate(Check, nullptr, 256 * 1024, 0x38, -2, false);
	if (!Worker) {
		Result = { CtrUpdateState::Error, {}, "Not enough memory to start update check." };
		Ready.store(true, std::memory_order_release);
	}
}

std::optional<CtrUpdateResult> CtrPollUpdate()
{
	if (!Ready.load(std::memory_order_acquire)) return std::nullopt;
	Stop();
	Ready = false;
	return Result;
}
} // namespace devilution
