#!/usr/bin/env python3
"""Check room naming overflow and privacy transitions using production functions."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
def function(file,signature):
 s=(ROOT/file).read_text();start=s.index(signature);opening=s.index('\n{',start)+1;end=opening+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
CPP=r'''
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <strings.h>
#include <vector>
#define SDL_strncasecmp strncasecmp
#define SDL_strcasecmp strcasecmp
namespace net {struct packet_out {static unsigned GenerateCookie(){static unsigned n=0;return n++;}};}
struct GameInfo{std::string name;};
std::vector<GameInfo> Gamelist, advertised;
std::vector<GameInfo> DvlNet_GetGamelist(){return advertised;}
constexpr int SELCONN_ZT=1,SELCONN_TCP=2;
int provider=SELCONN_ZT;
bool GameIsPublic;
std::string GamePassword;
struct Net{int clears=0;void clear_password(){++clears;}} mockNet;
Net*dvlnet_inst=&mockNet;
'''
TEST=r'''
int main(){
 assert(IsPrivateGameId("PR1234") && IsPrivateGameId("pr9876"));
 assert(!IsPrivateGameId("pretty") && !IsPrivateGameId("PR12345"));
 Gamelist={{"Public_2"}};advertised={{"Public_4"}};
 assert(GeneratePublicGameName()=="Public_5");
 advertised={{"Public_2147483647"},{"Public_99999999999999999999999999"}};
 assert(GeneratePublicGameName()=="Public_3");
 for(bool isPublic:{false,true}){
  provider=SELCONN_ZT;GameIsPublic=isPublic;GamePassword="old";
  DvlNet_ClearPassword();assert(GameIsPublic==isPublic&&GamePassword.empty());
  provider=SELCONN_TCP;GameIsPublic=isPublic;GamePassword="old";
  DvlNet_ClearPassword();assert(GameIsPublic&&GamePassword.empty());
 }
 assert(mockNet.clears==4);
 advertised={{"PR1000"}};Gamelist={{"PR1001"}};assert(GeneratePrivateGameId()=="PR1002");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 src=Path(tmp)/'test.cpp';exe=Path(tmp)/'test'
 src.write_text(CPP+function('Source/DiabloUI/multi/selgame.cpp','std::string GeneratePublicGameName()')+function('Source/DiabloUI/multi/selgame.cpp','bool IsPrivateGameId(')+function('Source/DiabloUI/multi/selgame.cpp','std::string GeneratePrivateGameId()')+function('Source/storm/storm_net.cpp','void DvlNet_ClearPassword()')+TEST)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: public room numbering handles oversized suffixes; LAN privacy resets after private rooms')
