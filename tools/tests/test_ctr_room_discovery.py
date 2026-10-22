#!/usr/bin/env python3
"""Run production room discovery and peer recognition with a fake transport."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'Source/dvlnet/base_protocol.h').read_text()
def fn(sig):
 start=s.index(sig);start=s.rfind('template <class P>',0,start);op=s.index('\n{',start)+1;end=op+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
cpp=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <expected>
#include <map>
#include <string>
#include <strings.h>
#include "platform/ctr/network_progress.hpp"
using namespace devilution;
#define SDL_strcasecmp strcasecmp
void CtrZeroTierLogStage(const char*,int=0){}
constexpr int MAX_PLRS=4;
struct PacketError{PacketError(const char*){}};
uint32_t now=0;int pumps=0;
uint32_t SDL_GetTicks(){return now;}
void SDL_Delay(int ms){now+=ms;}
template<class P>struct base_protocol{
 using endpoint_t=int;
 struct Info{int peer=7;};
 struct Peer{int endpoint=0;};
 std::array<Peer,MAX_PLRS> peers{};
 std::map<std::string,Info> game_list;
 std::string gamename="public_1";
 int firstpeer=0,requests=0;bool reply=false;
 void send_info_request(){++requests;}
 void recv(){if(reply&&now>=1200)game_list["Public_1"]={};}
 std::expected<void,PacketError>wait_firstpeer();
 bool is_recognized(endpoint_t sender);
};
'''
cpp+=fn('base_protocol<P>::wait_firstpeer()')+fn('base_protocol<P>::is_recognized(')
cpp+=r'''
int main(){
 CtrNetworkProgressCallback=[](){++pumps;};
 base_protocol<int>p;p.reply=true;assert(p.wait_firstpeer());
 assert(p.firstpeer==7&&p.gamename=="Public_1"&&p.requests==3);
 assert(pumps>50);
 now=0;pumps=0;base_protocol<int>missing;
 assert(!missing.wait_firstpeer());assert(now==15000&&missing.requests==30);
 assert(!missing.is_recognized(99)); // ASan catches the old peers[MAX_PLRS] read.
 missing.peers[3].endpoint=99;assert(missing.is_recognized(99));
}
'''
with tempfile.TemporaryDirectory() as t:
 src=Path(t)/'test.cpp';exe=Path(t)/'test';src.write_text(cpp)
 subprocess.run(['c++','-std=c++23','-D__3DS__','-fsanitize=address,undefined','-I'+str(ROOT/'Source'),str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: bounded multicast rate, case-insensitive rooms, live progress, timeout and peer-array bounds')
