#include "platform/ctr/update.hpp"
#include "platform/ctr/update_json.hpp"
#include "platform/ctr/sockets.hpp"
#include <3ds.h>
#include <curl/curl.h>
#include <mbedtls/debug.h>
#include <mbedtls/ssl.h>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include "utils/paths.h"

namespace devilution {
namespace {
std::atomic_bool Ready { false };
std::atomic_bool Stopping { false };
Thread Worker = nullptr;
CtrUpdateResult Result { CtrUpdateState::Error, {}, {} };
constexpr size_t MaxResponse = 512 * 1024;

struct TlsTrace {
	std::unique_ptr<FILE, decltype(&std::fclose)> log { std::fopen((paths::ConfigPath() + "network-update.log").c_str(), "wb"), std::fclose };
	int state = -1;
	char curve[32] {};
	char errorBuffer[CURL_ERROR_SIZE] {};
};

// Keep only negotiation metadata, never raw packets or key material.
void TlsDebug(void *user, int, const char *, int, const char *message)
{
	auto &trace = *static_cast<TlsTrace *>(user);
	if (std::strncmp(message, "client state: ", 14) == 0)
		trace.state = std::atoi(message + 14);
	else if (std::strncmp(message, "ECDH curve: ", 12) == 0) {
		std::snprintf(trace.curve, sizeof(trace.curve), "%s", message + 12);
		trace.curve[std::strcspn(trace.curve, "\r\n")] = '\0';
	}
}

CURLcode ConfigureTls(CURL *, void *context, void *user)
{
	// Mbed TLS's default advertises its largest curves first. Prefer the
	// efficient modern groups on ARM11; retain larger certificate curves.
	static constexpr mbedtls_ecp_group_id curves[] = {
#if defined(MBEDTLS_ECP_DP_CURVE25519_ENABLED)
		MBEDTLS_ECP_DP_CURVE25519,
#endif
#if defined(MBEDTLS_ECP_DP_SECP256R1_ENABLED)
		MBEDTLS_ECP_DP_SECP256R1,
#endif
#if defined(MBEDTLS_ECP_DP_SECP384R1_ENABLED)
		MBEDTLS_ECP_DP_SECP384R1,
#endif
#if defined(MBEDTLS_ECP_DP_SECP521R1_ENABLED)
		MBEDTLS_ECP_DP_SECP521R1,
#endif
		MBEDTLS_ECP_DP_NONE,
	};
	auto *config = static_cast<mbedtls_ssl_config *>(context);
	mbedtls_ssl_conf_curves(config, curves);
#if defined(MBEDTLS_DEBUG_C)
	mbedtls_debug_set_threshold(2);
	mbedtls_ssl_conf_dbg(config, TlsDebug, user);
#else
	(void)user;
#endif
	return CURLE_OK;
}

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

int Progress(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return Stopping.load(std::memory_order_relaxed) ? 1 : 0;
}

bool Get(CURL *curl, TlsTrace &trace, std::string_view url, std::string &body, std::string &error)
{
	if (!curl) { error = "HTTP initialization failed."; return false; }
	trace.state = -1;
	trace.curve[0] = '\0';
	char *errorBuffer = trace.errorBuffer;
	errorBuffer[0] = '\0';
	const std::string address { url };
	CURLcode setup = CURLE_OK;
	auto set = [&](CURLoption option, auto value) {
		if (setup == CURLE_OK) setup = curl_easy_setopt(curl, option, value);
	};
	set(CURLOPT_URL, address.c_str());
	set(CURLOPT_USERAGENT, "devil-3ds/" CTR_PORT_VERSION);
	// SOC is IPv4-only; portlibs supports TLS 1.2 and HTTP/1.1.
	set(CURLOPT_IPRESOLVE, static_cast<long>(CURL_IPRESOLVE_V4));
	set(CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2 | CURL_SSLVERSION_MAX_TLSv1_2));
	set(CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
	set(CURLOPT_CAINFO, "romfs:/cacert.pem");
	set(CURLOPT_SSL_VERIFYPEER, 1L);
	set(CURLOPT_SSL_VERIFYHOST, 2L);
	set(CURLOPT_SSL_CTX_FUNCTION, ConfigureTls);
	set(CURLOPT_SSL_CTX_DATA, &trace);
	set(CURLOPT_PROTOCOLS_STR, "https");
	set(CURLOPT_CONNECTTIMEOUT, 30L);
	set(CURLOPT_TIMEOUT, 60L);
	set(CURLOPT_NOSIGNAL, 1L);
	set(CURLOPT_NOPROGRESS, 0L);
	set(CURLOPT_XFERINFOFUNCTION, Progress);
	set(CURLOPT_ERRORBUFFER, errorBuffer);
	set(CURLOPT_WRITEFUNCTION, Receive);
	set(CURLOPT_WRITEDATA, &body);
	if (setup != CURLE_OK) {
		error = std::string("HTTP setup failed: ") + curl_easy_strerror(setup);
		return false;
	}
	const CURLcode code = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	double connected = 0, secured = 0, elapsed = 0;
	curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connected);
	curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &secured);
	curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &elapsed);
	if (trace.log) {
		std::fprintf(trace.log.get(), "Version %s; URL %s\nHTTP %ld; curl %d; TCP %.3fs; TLS %.3fs; total %.3fs; TLS state %d; curve %s\n%s\n",
		    CTR_PORT_VERSION, address.c_str(), status, static_cast<int>(code), connected, secured, elapsed, trace.state, trace.curve,
		    errorBuffer[0] ? errorBuffer : curl_easy_strerror(code));
		std::fflush(trace.log.get());
	}
	if (code != CURLE_OK) {
		if (code == CURLE_OPERATION_TIMEDOUT) {
			error = secured > 0 ? "GitHub response timed out."
			    : connected > 0 ? "TLS handshake timed out. Wi-Fi connected, but GitHub HTTPS did not complete."
			                    : "GitHub connection timed out. Check DNS or try another network.";
		} else {
			error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
		}
		return false;
	}
	if (status != 200) {
		const bool limited = status == 429 || (status == 403 && body.find("rate limit") != std::string::npos);
		error = limited ? "GitHub rate limit. Try again later." : "GitHub HTTP error: " + std::to_string(status);
		return false;
	}
	return true;
}

void Check(void *)
{
	CtrUpdateResult result { CtrUpdateState::Error, {}, {} };
#ifdef __cpp_exceptions
	try {
#endif
		if (!n3ds_socInit()) {
			result.detail = n3ds_networkError();
		} else {
			// The portlibs mbedTLS entropy source calls sslcGenerateRandomData.
			// It requires ssl:C to be initialized before TLS seeding.
			struct SslService {
				::Result status = sslcInit(0);
				~SslService() { if (R_SUCCEEDED(status)) sslcExit(); }
			} ssl;
			unsigned char randomProbe[32];
			const auto secureStatus = R_FAILED(ssl.status) ? ssl.status : sslcGenerateRandomData(randomProbe, sizeof(randomProbe));
			if (R_FAILED(secureStatus)) {
				char detail[100];
				std::snprintf(detail, sizeof(detail), "ssl:C %s failed (0x%08lX).",
				    R_FAILED(ssl.status) ? "initialization" : "random generation",
				    static_cast<unsigned long>(static_cast<uint32_t>(secureStatus)));
				result.detail = detail;
			} else if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
				result.detail = "HTTP initialization failed.";
			} else {
				struct CurlGlobalCleanup {
					~CurlGlobalCleanup() { curl_global_cleanup(); }
				} cleanup;
				TlsTrace trace;
				// Both requests use the same server. Reuse its connection/session
				// instead of parsing every CA and negotiating TLS a second time.
				std::string body;
				const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
				if (Get(handle.get(), trace, "https://api.github.com/repos/Karolynaz/devil-3ds/releases/latest", body, result.detail)) {
					const auto tag = CtrJsonString(body, "tag_name");
					if (!tag || !CtrValidReleaseTag(*tag)) {
						result.detail = "Invalid release response from GitHub.";
					} else {
						result.version = *tag;
						body.clear();
						const std::string url = std::string("https://api.github.com/repos/Karolynaz/devil-3ds/compare/") + CTR_BUILD_COMMIT + "..." + *tag + "?per_page=1";
						if (Get(handle.get(), trace, url, body, result.detail)) {
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
void CtrStopUpdateCheck()
{
	Stopping.store(true, std::memory_order_relaxed);
	Stop();
}

bool CtrUpdateBusy() { return Worker && !Ready.load(std::memory_order_acquire); }

void CtrCheckForUpdates()
{
	if (Stopping.load(std::memory_order_relaxed) || CtrUpdateBusy()) return;
	Stop();
	Ready = false;
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
