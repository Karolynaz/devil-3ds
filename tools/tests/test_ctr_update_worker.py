#!/usr/bin/env python3
"""Exercise the production update worker with fake SDK/HTTP services, without networking."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SDK = r'''
#pragma once
#include <cstddef>
#include <cstdint>
using u8=unsigned char;
using u64=uint64_t;
constexpr u64 SYSCLOCK_ARM11=1000;
inline u64 svcGetSystemTick() {static u64 tick=0;return tick+=100;}
using Result=int;
#define R_SUCCEEDED(r) ((r)>=0)
#define R_FAILED(r) ((r)<0)
int sslcInit(int); void sslcExit(); int sslcGenerateRandomData(u8 *,size_t);
struct FakeThread { void (*fn)(void *)=nullptr; void *arg=nullptr; };
using Thread = FakeThread *;
Thread threadCreate(void (*)(void *), void *, size_t, int, int, bool);
void threadJoin(Thread, uint64_t);
void threadFree(Thread);
'''
CURL = r'''
#pragma once
#include <cstddef>
#include <string>
using CURLcode = int;
using CURLoption = int;
using curl_off_t = long long;
constexpr int CURLE_OK=0, CURLE_WRITE_ERROR=23, CURLE_COULDNT_CONNECT=7, CURLE_OPERATION_TIMEDOUT=28;
constexpr int CURL_GLOBAL_DEFAULT=3, CURL_ERROR_SIZE=256, CURLINFO_RESPONSE_CODE=100, CURLINFO_CONNECT_TIME=101, CURLINFO_APPCONNECT_TIME=102, CURLINFO_TOTAL_TIME=103;
enum { CURLOPT_URL, CURLOPT_USERAGENT, CURLOPT_CAINFO, CURLOPT_PROTOCOLS_STR,
    CURLOPT_CONNECTTIMEOUT, CURLOPT_TIMEOUT, CURLOPT_NOSIGNAL, CURLOPT_ERRORBUFFER, CURLOPT_NOPROGRESS, CURLOPT_XFERINFOFUNCTION,
    CURLOPT_WRITEFUNCTION, CURLOPT_WRITEDATA, CURLOPT_IPRESOLVE, CURLOPT_SSLVERSION, CURLOPT_HTTP_VERSION,
    CURLOPT_SSL_CTX_FUNCTION, CURLOPT_SSL_CTX_DATA, CURLOPT_SSL_VERIFYPEER, CURLOPT_SSL_VERIFYHOST };
constexpr long CURL_IPRESOLVE_V4=1, CURL_SSLVERSION_TLSv1_2=6, CURL_SSLVERSION_MAX_TLSv1_2=6<<16, CURL_HTTP_VERSION_1_1=2;
struct CURL {
    std::string url;
    size_t (*writer)(char *,size_t,size_t,void *)=nullptr;
    void *user=nullptr;
    long status=200;
    double connected=0,secured=0;
    int (*progress)(void *,curl_off_t,curl_off_t,curl_off_t,curl_off_t)=nullptr;
    CURLcode (*tls)(CURL *,void *,void *)=nullptr;
    void *tlsUser=nullptr;
};
CURLcode curl_global_init(long);
void curl_global_cleanup();
CURL *curl_easy_init();
void curl_easy_cleanup(CURL *);
CURLcode curl_easy_perform(CURL *);
void curl_easy_getinfo(CURL *,int,long *);
void curl_easy_getinfo(CURL *,int,double *);
const char *curl_easy_strerror(CURLcode);
CURLcode curl_easy_setopt(CURL *,int,const char *);
CURLcode curl_easy_setopt(CURL *,int,long);
CURLcode curl_easy_setopt(CURL *,int,void *);
CURLcode curl_easy_setopt(CURL *,int,size_t (*)(char *,size_t,size_t,void *));
CURLcode curl_easy_setopt(CURL *,int,int (*)(void *,curl_off_t,curl_off_t,curl_off_t,curl_off_t));
CURLcode curl_easy_setopt(CURL *,int,CURLcode (*)(CURL *,void *,void *));
'''
TLS = r'''
#pragma once
#include <cassert>
#define MBEDTLS_DEBUG_C
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
enum mbedtls_ecp_group_id { MBEDTLS_ECP_DP_NONE, MBEDTLS_ECP_DP_CURVE25519,
    MBEDTLS_ECP_DP_SECP256R1, MBEDTLS_ECP_DP_SECP384R1, MBEDTLS_ECP_DP_SECP521R1 };
struct mbedtls_ssl_config {};
inline void mbedtls_ssl_conf_curves(mbedtls_ssl_config *,const mbedtls_ecp_group_id *groups) {
    assert(groups[0]==MBEDTLS_ECP_DP_CURVE25519 && groups[1]==MBEDTLS_ECP_DP_SECP256R1);
    assert(groups[2]==MBEDTLS_ECP_DP_SECP384R1 && groups[3]==MBEDTLS_ECP_DP_SECP521R1 && groups[4]==MBEDTLS_ECP_DP_NONE);
}
inline void mbedtls_debug_set_threshold(int level) {assert(level==2);}
inline void mbedtls_ssl_conf_dbg(mbedtls_ssl_config *,void (*fn)(void *,int,const char *,int,const char *),void *user) {
    fn(user,2,"ssl_cli.c",1,"client state: 4\n");
    fn(user,2,"ssl_cli.c",1,"client state: 4\n");
    fn(user,2,"ssl_cli.c",1,"ECDH curve: x25519\n");
    fn(user,2,"ssl_msg.c",1,"ssl->f_send() returned 100 (-0x0064)\n");
    fn(user,2,"ssl_msg.c",1,"ssl->f_send() returned -26880 (-0x6900)\n");
    fn(user,2,"ssl_msg.c",1,"ssl->f_recv(_timeout)() returned 200 (-0x00c8)\n");
    fn(user,2,"ssl_msg.c",1,"ssl->f_recv(_timeout)() returned 0 (-0x0000)\n");
    fn(user,2,"ssl_cli.c",1,"irrelevant debug data");
}
'''
HARNESS = r'''
#include <cassert>
#include <deque>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <3ds.h>
#include <curl/curl.h>
bool sslWorks=true, rngWorks=true, sslActive=false;
int sslInit=0, sslExit=0;
bool wifi=true, globalWorks=true, easyWorks=true, threadWorks=true;
bool deferWorker=false;
int priority=0, joins=0, frees=0, liveHandles=0, globalInit=0, globalCleanup=0, handlesCreated=0, setupFailure=-1;
struct Response { std::string body; long status=200; int code=CURLE_OK; int fault=0; double connected=0,secured=0; };
std::deque<Response> responses;
Thread threadCreate(void (*fn)(void *),void *arg,size_t stack,int p,int core,bool detached) {
    priority=p; assert(stack==256*1024 && core==-2 && !detached);
    if (!threadWorks) return nullptr;
    auto thread=new FakeThread; if(deferWorker) { thread->fn=fn;thread->arg=arg; } else fn(arg); return thread;
}
void threadJoin(Thread t,uint64_t) { ++joins; if(t->fn) { auto fn=t->fn;t->fn=nullptr;fn(t->arg); } }
void threadFree(Thread thread) { ++frees; delete thread; }
int sslcInit(int session) { assert(session==0 && !sslActive); if (!sslWorks) return -1; ++sslInit; sslActive=true; return 0; }
void sslcExit() { assert(sslActive); sslActive=false; ++sslExit; }
int sslcGenerateRandomData(u8 *,size_t size) { assert(sslActive && size==32); return rngWorks ? 0 : -1; }
CURLcode curl_global_init(long) { assert(sslActive); if (!globalWorks) return 1; ++globalInit; return CURLE_OK; }
void curl_global_cleanup() { ++globalCleanup; }
CURL *curl_easy_init() { if (!easyWorks) return nullptr; ++liveHandles; ++handlesCreated; return new CURL; }
void curl_easy_cleanup(CURL *curl) { assert(liveHandles>0); --liveHandles; delete curl; }
CURLcode curl_easy_setopt(CURL *curl,int option,const char *value) {
    if (option==CURLOPT_URL) curl->url=value;
    if (option==CURLOPT_CAINFO) assert(std::string(value)=="romfs:/cacert.pem");
    if (option==CURLOPT_PROTOCOLS_STR) assert(std::string(value)=="https");
    return option==setupFailure ? 1 : CURLE_OK;
}
CURLcode curl_easy_setopt(CURL *,int option,long value) {
    if (option==CURLOPT_IPRESOLVE) assert(value==CURL_IPRESOLVE_V4);
    if (option==CURLOPT_SSLVERSION) assert(value==(CURL_SSLVERSION_TLSv1_2 | CURL_SSLVERSION_MAX_TLSv1_2));
    if (option==CURLOPT_HTTP_VERSION) assert(value==CURL_HTTP_VERSION_1_1);
    if (option==CURLOPT_NOPROGRESS) assert(value==0);
    if (option==CURLOPT_TIMEOUT) assert(value==60);
    if (option==CURLOPT_CONNECTTIMEOUT) assert(value==30);
    if (option==CURLOPT_SSL_VERIFYPEER) assert(value==1);
    if (option==CURLOPT_SSL_VERIFYHOST) assert(value==2);
    return option==setupFailure ? 1 : CURLE_OK;
}
CURLcode curl_easy_setopt(CURL *curl,int option,void *user) {
    if(option==CURLOPT_SSL_CTX_DATA) curl->tlsUser=user;
    else {assert(option==CURLOPT_WRITEDATA);curl->user=user;}
    return option==setupFailure ? 1 : CURLE_OK;
}
CURLcode curl_easy_setopt(CURL *curl,int option,CURLcode (*tls)(CURL *,void *,void *)) {
    assert(option==CURLOPT_SSL_CTX_FUNCTION);curl->tls=tls;return option==setupFailure ? 1 : CURLE_OK;
}
CURLcode curl_easy_setopt(CURL *curl,int option,size_t (*writer)(char *,size_t,size_t,void *)) {
    assert(option==CURLOPT_WRITEFUNCTION); curl->writer=writer;
    return option==setupFailure ? 1 : CURLE_OK;
}
CURLcode curl_easy_setopt(CURL *curl,int option,int (*progress)(void *,curl_off_t,curl_off_t,curl_off_t,curl_off_t)) {
    assert(option==CURLOPT_XFERINFOFUNCTION);curl->progress=progress;
    return option==setupFailure ? 1 : CURLE_OK;
}
CURLcode curl_easy_perform(CURL *curl) {
    assert(sslActive);
    assert(curl->progress);
    if(curl->progress(nullptr,0,0,0,0)) return 42;
    assert(curl->tls && curl->tlsUser);assert(curl->tls(curl,nullptr,curl->tlsUser)==CURLE_OK);
    assert(!responses.empty()); auto response=std::move(responses.front()); responses.pop_front();
    assert(curl->url.starts_with("https://api.github.com/repos/Karolynaz/devil-3ds/"));
#ifdef __cpp_exceptions
    if (response.fault==1) throw std::bad_alloc();
    if (response.fault==2) throw std::runtime_error("Service fault");
#endif
    curl->status=response.status;curl->connected=response.connected;curl->secured=response.secured;
    if (response.code!=CURLE_OK) return response.code;
    return curl->writer(response.body.data(),1,response.body.size(),curl->user)==response.body.size()
        ? CURLE_OK : CURLE_WRITE_ERROR;
}
void curl_easy_getinfo(CURL *curl,int,long *status) { *status=curl->status; }
void curl_easy_getinfo(CURL *curl,int info,double *value) { *value=info==CURLINFO_CONNECT_TIME?curl->connected:curl->secured; }
const char *curl_easy_strerror(CURLcode) { return "HTTP transfer failed."; }
namespace devilution { bool n3ds_socInit() { return wifi; } std::string n3ds_networkError() { return "mock network failure"; } }
namespace devilution::paths { const std::string &ConfigPath() {static const std::string path=TEST_LOG_DIRECTORY;return path;} }
#include "platform/ctr/update.cpp"
using namespace devilution;
CtrUpdateResult run() {
    CtrCheckForUpdates(); assert(!CtrUpdateBusy());
    auto result=CtrPollUpdate(); assert(result && !CtrPollUpdate());
    assert(liveHandles==0 && globalInit==globalCleanup && joins==frees);
    assert(responses.empty() && !sslActive && sslInit==sslExit); return *result;
}
int main() {
    for (auto status : {"ahead", "identical", "behind", "diverged"}) {
        int before=handlesCreated;
        responses.push_back({R"({"tag_name":"v1.2.3"})"});
        responses.push_back({std::string("{\"status\":\"")+status+"\"}"});
        auto result=run(); assert(priority==0x38 && result.version=="v1.2.3");
        assert(handlesCreated==before+1); // Both requests share one connection cache.
        assert(result.state==(std::string(status)=="ahead" ? CtrUpdateState::Available
            : std::string(status)=="diverged" ? CtrUpdateState::Different : CtrUpdateState::Current));
    }
    assert(CtrPortVersion()=="3.01");
    sslWorks=false; assert(run().detail=="ssl:C initialization failed (0xFFFFFFFF)."); sslWorks=true;
    rngWorks=false; assert(run().detail=="ssl:C random generation failed (0xFFFFFFFF)."); rngWorks=true;
    wifi=false; assert(run().detail=="mock network failure"); wifi=true;
    globalWorks=false; assert(run().state==CtrUpdateState::Error); globalWorks=true;
    easyWorks=false; assert(run().state==CtrUpdateState::Error); easyWorks=true;
    threadWorks=false; assert(run().state==CtrUpdateState::Error); threadWorks=true;
    responses.push_back({"",429}); assert(run().detail=="GitHub rate limit. Try again later.");
    responses.push_back({"Forbidden",403}); assert(run().detail=="GitHub HTTP error: 403");
    responses.push_back({"API rate limit exceeded",403}); assert(run().detail=="GitHub rate limit. Try again later.");
    setupFailure=CURLOPT_SSL_CTX_FUNCTION;assert(run().detail.starts_with("HTTP setup failed:"));setupFailure=-1;
    responses.push_back({"",200,CURLE_COULDNT_CONNECT}); assert(run().state==CtrUpdateState::Error);
    {
        std::ifstream file(paths::ConfigPath()+"network-update.log");
        const std::string log((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        assert(log.find("TLS state 4; curve x25519")!=std::string::npos && log.find("curl 7")!=std::string::npos);
        assert(log.find("TLS phase 4 at 0.100s")!=std::string::npos);
        assert(log.find("TLS phase 4",log.find("TLS phase 4")+1)==std::string::npos);
        assert(log.find("TLS bytes sent 100; received 200; last send -26880; last receive 0")!=std::string::npos);
    }
    responses.push_back({"",200,CURLE_OPERATION_TIMEDOUT,0,0,0}); assert(run().detail.starts_with("GitHub connection timed out."));
    responses.push_back({"",200,CURLE_OPERATION_TIMEDOUT,0,0.1,0}); assert(run().detail.starts_with("TLS handshake timed out."));
    responses.push_back({"",200,CURLE_OPERATION_TIMEDOUT,0,0.1,0.5}); assert(run().detail=="GitHub response timed out.");
    responses.push_back({R"({"tag_name":"v1/../../bad"})"}); assert(run().state==CtrUpdateState::Error);
    responses.push_back({R"({"tag_name":"v1"})"}); responses.push_back({"bad JSON"});
    assert(run().state==CtrUpdateState::Error);
    responses.push_back({std::string(512*1024+1,'x')}); assert(run().state==CtrUpdateState::Error);
    std::string body; char data[]="abc";
    assert(Receive(data,3,1,&body)==3 && body=="abc");
    assert(Receive(data,std::numeric_limits<size_t>::max(),2,&body)==0 && body=="abc");
    body.assign(512*1024,'x'); assert(Receive(data,1,1,&body)==0);
#ifdef __cpp_exceptions
    responses.push_back({"",200,CURLE_OK,1}); assert(run().detail=="Out of memory.");
    responses.push_back({"",200,CURLE_OK,2}); assert(run().detail=="Check failed.");
#endif
    deferWorker=true;CtrCheckForUpdates();assert(CtrUpdateBusy());
    CtrStopUpdateCheck();assert(!Worker && !CtrUpdateBusy() && Progress(nullptr,0,0,0,0)==1);
    assert(liveHandles==0 && joins==frees && !sslActive && globalInit==globalCleanup);
    int before=joins;CtrStopUpdateCheck();CtrCheckForUpdates();assert(joins==before && !Worker);
}
'''
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    (temp / 'curl').mkdir()
    (temp / 'mbedtls').mkdir()
    (temp / 'mbedtls/ssl.h').write_text(TLS)
    (temp / 'mbedtls/debug.h').write_text('#include "ssl.h"\n')
    (temp / '3ds.h').write_text(SDK)
    (temp / 'curl/curl.h').write_text(CURL)
    (temp / 'worker.cpp').write_text(HARNESS)
    for exceptions in (True, False):
        exe = temp / ('worker' if exceptions else 'worker-no-exceptions')
        command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-DCTR_BUILD_COMMIT="test-commit"', '-DCTR_PORT_VERSION="3.01"',
            '-DTEST_LOG_DIRECTORY="'+str(temp)+'/"',
            '-I'+str(temp), '-I'+str(ROOT/'Source'), '-I'+str(ROOT/'3rdParty/jsmn')]
        if not exceptions:
            command.append('-fno-exceptions')
        subprocess.run(command + [str(temp/'worker.cpp'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print('PASS: update worker results, HTTP/Wi-Fi failures, bounded callbacks and cleanup, with/without exceptions (ASan/UBSan)')
