#!/usr/bin/env python3
"""Exercise production ZeroTier events after a brief offline interval."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
def fn(path,signature):
 s=(ROOT/path).read_text();start=s.index(signature);op=s.index('\n{',start)+1;end=op+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
events=['NODE_ONLINE','NODE_OFFLINE','NETWORK_READY_IP6','ADDR_ADDED_IP6','NETWORK_NOT_FOUND','NETWORK_ACCESS_DENIED','NETWORK_CLIENT_TOO_OLD','NODE_FATAL_ERROR','NETWORK_DOWN','PEER_DIRECT','PEER_RELAY','PEER_PATH_DEAD']
cpp=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <map>
using zts_event_t=int;
struct Info{uint64_t node_id=1,net_id=2,peer_id=3;int addr=0;};
struct zts_event_msg_t{int event_code;Info*node;Info*network;Info*addr;Info*peer;};
std::atomic_bool ShuttingDown=false,zt_node_online=false,zt_network_ready=false,zt_joined=false;
std::atomic_uint zt_peers_ready=0;
std::atomic_int NetworkError=0;
std::map<uint64_t,int> ztPeerEvents;
constexpr uint64_t ZtNetwork=2;
constexpr int ZTS_ERR_OK=0;
uint32_t now=0;
uint32_t SDL_GetTicks(){return now;}
void ctr_zt_trace(unsigned,int){}
void CtrZeroTierLogStage(const char*,int=0){}
template<class... A>void Log(const char*,A...){}
int joins=0;
int zts_net_join(uint64_t){++joins;return 0;}
void zt_ip6setup(){}
void print_ip6_addr(void*){}
struct PeerEventGuard{};
'''
cpp+='enum {'+','.join('ZTS_EVENT_'+e for e in events)+'};\n'
cpp+=fn('Source/dvlnet/zerotier_native.cpp','bool zerotier_peers_ready()')
cpp+=fn('Source/dvlnet/zerotier_native.cpp','void Callback(')
cpp+=fn('Source/dvlnet/zerotier_native.cpp','bool zerotier_network_ready()')
cpp+=r'''
int main(){
 Info info;
 auto event=[&](int code){zts_event_msg_t msg{code,&info,&info,&info,&info};Callback(&msg);};
 event(ZTS_EVENT_NODE_ONLINE);event(ZTS_EVENT_NETWORK_READY_IP6);
 assert(zerotier_network_ready()&&joins==1);
 // Churning peer paths must not debounce discovery indefinitely.
 for(now=1000;now<=6000;now+=1000)event(ZTS_EVENT_PEER_DIRECT);
 assert(zerotier_peers_ready());
 event(ZTS_EVENT_NODE_OFFLINE);assert(!zerotier_network_ready());
 event(ZTS_EVENT_NODE_ONLINE);assert(zerotier_network_ready()&&joins==1);
 event(ZTS_EVENT_NETWORK_DOWN);assert(!zerotier_network_ready());
 event(ZTS_EVENT_NETWORK_READY_IP6);assert(zerotier_network_ready());
}
'''
with tempfile.TemporaryDirectory() as t:
 src=Path(t)/'test.cpp';exe=Path(t)/'test';src.write_text(cpp)
 subprocess.run(['c++','-std=c++17','-D__3DS__','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: offline/online recovery, network down recovery and discovery under peer churn')
