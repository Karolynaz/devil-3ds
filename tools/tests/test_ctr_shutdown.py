#!/usr/bin/env python3
"""Exercise the production 3DS exit ordering with concurrent fake SDK workers.
Usage: test_ctr_shutdown.py PATH_TO_PATCHED_LIBZT
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DEP = Path(sys.argv[1])


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


controls = (DEP / 'src/Controls.cpp').read_text()
sys_arch = (DEP / 'ext/lwip-contrib/ports/unix/port/sys_arch.c').read_text()
native = (ROOT / 'Source/dvlnet/zerotier_native.cpp').read_text()
tcpip = (DEP / 'ext/lwip/src/api/tcpip.c').read_text()
assert '&ctr_node_thread, &service_stack' in controls and 'threadDetach(callback_thread)' not in controls
assert '&stack,' in sys_arch
assert 'UNLOCK_TCPIP_CORE();\n      return;' in tcpip
assert 'while (!_has_started)' in (DEP / 'src/VirtualTap.cpp').read_text()

prefix = r'''
#include <cassert>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdlib>
using Thread=std::thread *;
std::atomic_bool stopNode=false,stopDriver=false,stopCore=false;
std::atomic_int nodeLive=0,driverLive=0,coreLive=0,callbackLive=0;
int joins=0,frees=0;
void threadJoin(Thread t,uint64_t) { if(t && t->joinable()) t->join(); ++joins; }
void threadFree(Thread t) { assert(t && !t->joinable()); delete t; ++frees; }
int ctr_zt_join(Thread t,void **) { if(t) { threadJoin(t,UINT64_MAX);threadFree(t); } return 0; }
struct Mutex { std::mutex mutex; struct Lock { std::lock_guard<std::mutex> guard; Lock(Mutex &m):guard(m.mutex) {} }; } service_m;
std::mutex threads_mutex;
void ctr_zt_mutex_lock(std::mutex *m) { m->lock(); }
void ctr_zt_mutex_unlock(std::mutex *m) { m->unlock(); }
struct sys_thread { sys_thread *next; Thread pthread; };
sys_thread *threads=nullptr;
struct Events {
 std::atomic_bool running=true;
 void clrState(int) { running=false; }
 ~Events() { assert(!nodeLive && !driverLive && !coreLive && !callbackLive); }
} *zts_events=nullptr;
struct NodeService {
 void terminate() { stopNode=true; }
 ~NodeService() { assert(!nodeLive); }
} *zts_service=nullptr;
Thread ctr_node_thread=nullptr,ctr_callback_thread=nullptr;
constexpr int ZTS_STATE_CALLBACKS_RUNNING=8;
void zts_lwip_driver_shutdown() { assert(!nodeLive);stopDriver=true; }
void ctr_tcpip_shutdown() { assert(!nodeLive);stopCore=true; }
'''
suffix = r'''
namespace devilution::net {
Thread StartupWorker=nullptr;
std::atomic_bool ShuttingDown=false,zt_network_ready=true,zt_node_online=true;
struct PeerEventGuard {};
std::vector<int> ztPeerEvents {1,2};
'''
end = r'''
}
void setup(bool service,bool callback,bool stack) {
 zts_events=new Events;
 if(service) {
  zts_service=new NodeService;nodeLive=1;
  ctr_node_thread=new std::thread([] {
   while(!stopNode) std::this_thread::yield();
   // Tap destructors must still be able to enter the TCP/IP core.
   if(threads) assert(coreLive);
   nodeLive=0;
  });
 }
 if(stack) {
  driverLive=coreLive=1;
  auto driver=new std::thread([] { while(!stopDriver) std::this_thread::yield();driverLive=0; });
  auto core=new std::thread([] { while(!stopCore) std::this_thread::yield();coreLive=0; });
  threads=static_cast<sys_thread *>(std::malloc(sizeof(sys_thread)));
  threads->pthread=core;
  threads->next=static_cast<sys_thread *>(std::malloc(sizeof(sys_thread)));
  threads->next->pthread=driver;threads->next->next=nullptr;
 }
 if(callback) {
  callbackLive=1;
  ctr_callback_thread=new std::thread([] { while(zts_events->running) std::this_thread::yield();callbackLive=0; });
 }
}
int main() {
 using namespace devilution::net;
 for(int scenario=0;scenario<5;scenario++) {
  ShuttingDown=false;stopNode=stopDriver=stopCore=false;
  zt_network_ready=zt_node_online=true;
  // No multiplayer ever opened; failed service/callback startup; full node.
  if(scenario) setup(scenario>=3,scenario>=2,scenario>=2);
  // Exit while asynchronous initialization has not returned yet.
  if(scenario==4) StartupWorker=new std::thread([] { std::this_thread::yield(); });
  zerotier_network_shutdown();
  assert(!StartupWorker && !zts_service && !zts_events && !threads);
  assert(!nodeLive && !driverLive && !coreLive && !callbackLive);
  assert(!zt_network_ready && !zt_node_online && ztPeerEvents.empty());
  assert(joins==frees);
  int before=joins;zerotier_network_shutdown();assert(joins==before);
 }
}
'''
code = prefix + function(sys_arch, 'void ctr_zt_join_sys_threads(void)') + '\n'
code += function(controls, 'void zts_ctr_shutdown()') + '\n'
code += suffix + function(native, 'void zerotier_network_shutdown()') + end
tcpip_prefix = r"""
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
using u32_t=unsigned;
struct tcpip_msg {
 int type;
 struct { struct { void (*function)(void *);void *ctx; } cb; } msg;
};
constexpr int TCPIP_MSG_CALLBACK_STATIC=1;
std::mutex core,queueMutex;
std::condition_variable posted;
std::queue<void *> queue;
struct sys_mbox {} tcpip_mbox;
int ctr_tcpip_initialized=1,ctr_tcpip_stopping=0;
void (*tcpip_init_done)(void *)=nullptr;
void *tcpip_init_done_arg=nullptr;
#define LWIP_UNUSED_ARG(x) (void)(x)
#define LWIP_MARK_TCPIP_THREAD() ((void)0)
#define LOCK_TCPIP_CORE() core.lock()
#define UNLOCK_TCPIP_CORE() core.unlock()
#define LWIP_TCPIP_THREAD_ALIVE() ((void)0)
#define LWIP_DEBUGF(...) ((void)0)
#define LWIP_ASSERT(msg,cond) assert(cond)
void sys_mbox_post(sys_mbox *,void *msg) {
 std::lock_guard<std::mutex> lock(queueMutex);queue.push(msg);posted.notify_one();
}
void fetch(sys_mbox *,void **msg) {
 UNLOCK_TCPIP_CORE();
 {
  std::unique_lock<std::mutex> lock(queueMutex);
  posted.wait(lock,[] { return !queue.empty(); });
  *msg=queue.front();queue.pop();
 }
 LOCK_TCPIP_CORE();
}
#define TCPIP_MBOX_FETCH fetch
void tcpip_thread_handle_msg(tcpip_msg *msg) {
 assert(msg->type==TCPIP_MSG_CALLBACK_STATIC);msg->msg.cb.function(msg->msg.cb.ctx);
}
"""
tcpip_code = tcpip_prefix
for signature in ['tcpip_thread(void *arg)', 'static void ctr_tcpip_stop_callback(', 'void ctr_tcpip_shutdown(void)']:
    tcpip_code += ('static void\n' if signature.startswith('tcpip_thread(') else '') + function(tcpip, signature) + '\n'
tcpip_code += r"""
int main() {
 std::thread worker([] { tcpip_thread(nullptr); });
 ctr_tcpip_shutdown();ctr_tcpip_shutdown();worker.join();
 assert(ctr_tcpip_stopping && queue.empty());
 // Worker must release the core lock before returning.
 assert(core.try_lock());core.unlock();
}
"""
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    (path / 'tcpip.cpp').write_text(tcpip_code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-pthread', '-Wall', '-Wextra',
                    '-Werror', '-Wno-unused-variable', '-fsanitize=address,undefined',
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=20)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-pthread', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(path / 'tcpip.cpp'), '-o', str(path / 'tcpip')], check=True)
    subprocess.run([str(path / 'tcpip')], check=True, timeout=20)
print('PASS: exit waits for startup, node/taps, lwIP and callback workers; failures and repeated cleanup are safe (ASan/UBSan)')
