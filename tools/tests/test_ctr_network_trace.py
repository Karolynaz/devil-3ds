#!/usr/bin/env python3
"""Exercise the actual bounded trace queue and background writer with native API mocks."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
stubs={
'3ds.h':r'''#pragma once
#include <cstdint>
#include <mutex>
#include <thread>
#include <chrono>
using s64=int64_t;
struct LightLock {std::mutex lock;LightLock(int) {}};
inline void LightLock_Lock(LightLock *l){l->lock.lock();}
inline void LightLock_Unlock(LightLock *l){l->lock.unlock();}
struct HostThread {std::thread thread;HostThread(void(*fn)(void*),void *arg):thread(fn,arg){}};
using Thread=HostThread*;
inline Thread threadCreate(void(*fn)(void*),void*arg,size_t stack,int priority,int,bool){
 if(stack!=64*1024 || priority!=0x37) std::abort();return new HostThread(fn,arg);
}
inline void threadJoin(Thread t,uint64_t){t->thread.join();}
inline void threadFree(Thread t){delete t;}
inline void svcSleepThread(s64){std::this_thread::sleep_for(std::chrono::milliseconds(1));}
inline int APT_CheckNew3DS(bool *v){*v=true;return 0;}
''',
'SDL.h':r'''#pragma once
#include <atomic>
#include <cstdint>
inline uint32_t SDL_GetTicks(){static std::atomic_uint ticks{100};return ticks.fetch_add(100);}
''',
'utils/paths.h':r'''#pragma once
#include <cstdlib>
#include <string>
namespace devilution::paths {inline std::string ConfigPath(){return std::string(std::getenv("TRACE_TEST_DIR"))+"/";}}
''',
'utils/log.hpp':r'''#pragma once
namespace devilution {template<class...T>void LogError(const char*,T&&...) {}}
''',
'dvlnet/zerotier_native.h':r'''#pragma once
#include <cstdio>
namespace devilution::net {
inline int zerotier_startup_state(){return 2;}
inline bool zerotier_node_online(){return false;}
inline bool zerotier_network_ready(){return false;}
inline int zerotier_network_error(){return 0;}
inline int zerotier_startup_error(){return 0;}
inline void zerotier_log_diagnostics(FILE*f){std::fprintf(f,"transport snapshot\n");}
}
''',
'platform/ctr/sockets.hpp':'#pragma once\n',
}
code=r'''
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include "platform/ctr/network_trace_queue.hpp"
#include "platform/ctr/zerotier_log.hpp"
using namespace devilution;
int main(){
 CtrNetworkTraceQueue q;CtrNetworkTrace out{};
 assert(!q.pop(out));
 for(int i=0;i<128;++i)assert(q.push({uint32_t(i),0,i}));
 assert(!q.push({999,0,999}));assert(q.takeDropped()==1);assert(q.takeDropped()==0);
 for(int i=0;i<64;++i){assert(q.pop(out));assert(out.value==i);}
 for(int i=128;i<192;++i)assert(q.push({uint32_t(i),0,i}));
 for(int i=64;i<192;++i){assert(q.pop(out));assert(out.value==i);}
 assert(!q.pop(out));
 CtrZeroTierLogStage("before initialization");
 CtrZeroTierLogStart();
 CtrZeroTierLogStage("Wi-Fi ready");
 std::thread a([]{for(int i=0;i<32;++i)CtrZeroTierLogTrace(1,200+i);});
 std::thread b([]{for(int i=0;i<32;++i)CtrZeroTierLogTrace(6,10+i);});
 a.join();b.join();
 CtrZeroTierLogStage("cancelled; diagnostics continue");
 std::this_thread::sleep_for(std::chrono::milliseconds(10));
 CtrZeroTierLogStage("after menu closed");
 CtrZeroTierLogStop();
 std::string path=std::string(std::getenv("TRACE_TEST_DIR"))+"/network-zerotier.log";
 std::ifstream f(path);std::string log((std::istreambuf_iterator<char>(f)),{});
 assert(log.find("version test")!=std::string::npos);
 assert(log.find("Wi-Fi ready")!=std::string::npos);
 assert(log.find("after menu closed")!=std::string::npos);
 assert(log.find("trace kind 1; value 200")!=std::string::npos);
 assert(log.find("trace kind 6; value 10")!=std::string::npos);
 assert(log.find("transport snapshot")!=std::string::npos);
 assert(log.find("Diagnostic session ended")!=std::string::npos);
 assert(log.find("before initialization")==std::string::npos);
 // Reopening app diagnostics appends rather than deleting the previous attempt.
 CtrZeroTierLogStart();CtrZeroTierLogStage("second attempt");CtrZeroTierLogStop();
 std::ifstream f2(path);log.assign(std::istreambuf_iterator<char>(f2),{});
 assert(log.find("Wi-Fi ready")!=std::string::npos && log.find("second attempt")!=std::string::npos);
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 for name,data in stubs.items():
  f=p/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(data)
 (p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-DCTR_PORT_VERSION="test"','-DCTR_BUILD_COMMIT="local-uncommitted"','-I'+str(p),'-I'+str(ROOT/'Source'),str(ROOT/'Source/platform/ctr/zerotier_log.cpp'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 import os
 subprocess.run([str(p/'test')],env={**os.environ,'TRACE_TEST_DIR':str(p)},check=True)
print('PASS: trace FIFO, bounded overflow, concurrent producers, background writing, snapshots, continued logging, shutdown drain and append (ASan/UBSan)')
