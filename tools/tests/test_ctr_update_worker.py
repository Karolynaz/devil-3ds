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
struct FakeThread {};
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
constexpr int CURLE_OK=0, CURLE_WRITE_ERROR=23, CURLE_COULDNT_CONNECT=7;
constexpr int CURL_GLOBAL_DEFAULT=3, CURL_ERROR_SIZE=256, CURLINFO_RESPONSE_CODE=100;
enum { CURLOPT_URL, CURLOPT_USERAGENT, CURLOPT_CAINFO, CURLOPT_PROTOCOLS_STR,
    CURLOPT_CONNECTTIMEOUT, CURLOPT_TIMEOUT, CURLOPT_NOSIGNAL, CURLOPT_ERRORBUFFER,
    CURLOPT_WRITEFUNCTION, CURLOPT_WRITEDATA };
struct CURL {
    std::string url;
    size_t (*writer)(char *,size_t,size_t,void *)=nullptr;
    void *user=nullptr;
    long status=200;
};
CURLcode curl_global_init(long);
void curl_global_cleanup();
CURL *curl_easy_init();
void curl_easy_cleanup(CURL *);
CURLcode curl_easy_perform(CURL *);
void curl_easy_getinfo(CURL *,int,long *);
const char *curl_easy_strerror(CURLcode);
void curl_easy_setopt(CURL *,int,const char *);
void curl_easy_setopt(CURL *,int,long);
void curl_easy_setopt(CURL *,int,void *);
void curl_easy_setopt(CURL *,int,size_t (*)(char *,size_t,size_t,void *));
'''
HARNESS = r'''
#include <cassert>
#include <deque>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <3ds.h>
#include <curl/curl.h>
bool wifi=true, globalWorks=true, easyWorks=true, threadWorks=true;
int priority=0, joins=0, frees=0, liveHandles=0, globalInit=0, globalCleanup=0;
struct Response { std::string body; long status=200; int code=CURLE_OK; int fault=0; };
std::deque<Response> responses;
Thread threadCreate(void (*fn)(void *),void *arg,size_t stack,int p,int core,bool detached) {
    priority=p; assert(stack==256*1024 && core==-2 && !detached);
    if (!threadWorks) return nullptr;
    auto thread=new FakeThread; fn(arg); return thread;
}
void threadJoin(Thread,uint64_t) { ++joins; }
void threadFree(Thread thread) { ++frees; delete thread; }
CURLcode curl_global_init(long) { if (!globalWorks) return 1; ++globalInit; return CURLE_OK; }
void curl_global_cleanup() { ++globalCleanup; }
CURL *curl_easy_init() { if (!easyWorks) return nullptr; ++liveHandles; return new CURL; }
void curl_easy_cleanup(CURL *curl) { assert(liveHandles>0); --liveHandles; delete curl; }
void curl_easy_setopt(CURL *curl,int option,const char *value) {
    if (option==CURLOPT_URL) curl->url=value;
    if (option==CURLOPT_CAINFO) assert(std::string(value)=="romfs:/cacert.pem");
    if (option==CURLOPT_PROTOCOLS_STR) assert(std::string(value)=="https");
}
void curl_easy_setopt(CURL *,int option,long value) {
    if (option==CURLOPT_TIMEOUT) assert(value==20);
    if (option==CURLOPT_CONNECTTIMEOUT) assert(value==10);
}
void curl_easy_setopt(CURL *curl,int option,void *user) { assert(option==CURLOPT_WRITEDATA); curl->user=user; }
void curl_easy_setopt(CURL *curl,int option,size_t (*writer)(char *,size_t,size_t,void *)) {
    assert(option==CURLOPT_WRITEFUNCTION); curl->writer=writer;
}
CURLcode curl_easy_perform(CURL *curl) {
    assert(!responses.empty()); auto response=std::move(responses.front()); responses.pop_front();
    assert(curl->url.starts_with("https://api.github.com/repos/Karolynaz/devil-3ds/"));
#ifdef __cpp_exceptions
    if (response.fault==1) throw std::bad_alloc();
    if (response.fault==2) throw std::runtime_error("Service fault");
#endif
    curl->status=response.status;
    if (response.code!=CURLE_OK) return response.code;
    return curl->writer(response.body.data(),1,response.body.size(),curl->user)==response.body.size()
        ? CURLE_OK : CURLE_WRITE_ERROR;
}
void curl_easy_getinfo(CURL *curl,int,long *status) { *status=curl->status; }
const char *curl_easy_strerror(CURLcode) { return "HTTP transfer failed."; }
namespace devilution { bool n3ds_socInit() { return wifi; } }
#include "platform/ctr/update.cpp"
using namespace devilution;
CtrUpdateResult run() {
    CtrCheckForUpdates(); assert(!CtrUpdateBusy());
    auto result=CtrPollUpdate(); assert(result && !CtrPollUpdate());
    assert(liveHandles==0 && globalInit==globalCleanup && joins==frees);
    assert(responses.empty()); return *result;
}
int main() {
    for (auto status : {"ahead", "identical", "behind", "diverged"}) {
        responses.push_back({R"({"tag_name":"v1.2.3"})"});
        responses.push_back({std::string("{\"status\":\"")+status+"\"}"});
        auto result=run(); assert(priority==0x38 && result.version=="v1.2.3");
        assert(result.state==(std::string(status)=="ahead" ? CtrUpdateState::Available
            : std::string(status)=="diverged" ? CtrUpdateState::Different : CtrUpdateState::Current));
    }
    wifi=false; assert(run().state==CtrUpdateState::Error); wifi=true;
    globalWorks=false; assert(run().state==CtrUpdateState::Error); globalWorks=true;
    easyWorks=false; assert(run().state==CtrUpdateState::Error); easyWorks=true;
    threadWorks=false; assert(run().state==CtrUpdateState::Error); threadWorks=true;
    responses.push_back({"",429}); assert(run().detail=="GitHub rate limit. Try again later.");
    responses.push_back({"",200,CURLE_COULDNT_CONNECT}); assert(run().state==CtrUpdateState::Error);
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
}
'''
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    (temp / 'curl').mkdir()
    (temp / '3ds.h').write_text(SDK)
    (temp / 'curl/curl.h').write_text(CURL)
    (temp / 'worker.cpp').write_text(HARNESS)
    for exceptions in (True, False):
        exe = temp / ('worker' if exceptions else 'worker-no-exceptions')
        command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-DCTR_BUILD_COMMIT="test-commit"',
            '-I'+str(temp), '-I'+str(ROOT/'Source'), '-I'+str(ROOT/'3rdParty/jsmn')]
        if not exceptions:
            command.append('-fno-exceptions')
        subprocess.run(command + [str(temp/'worker.cpp'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print('PASS: update worker results, HTTP/Wi-Fi failures, bounded callbacks and cleanup, with/without exceptions (ASan/UBSan)')
