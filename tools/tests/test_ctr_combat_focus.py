#!/usr/bin/env python3
"""Production controller focus: nearby reachable threats beat loot, not walls/NPCs."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'Source/controls/plrctrls.cpp').read_text()
def fn(sig):
 a=s.index(sig);b=s.index('{',a);c=b+1;d=1
 while d:d+=(s[c]=='{')-(s[c]=='}');c+=1
 return s[a:c]
code=r'''
#include <cstdlib>
#include <cstdio>
#include <string>
#include "engine/point.hpp"
#define __3DS__ 1
#define check(v) do{if(!(v)){std::fprintf(stderr,"line %d\n",__LINE__);std::abort();}}while(0)
using namespace devilution;
enum class ControlTypes {KeyboardAndMouse,Gamepad};ControlTypes ControlMode=ControlTypes::Gamepad;
enum class PlayerActionType {None,Attack,Spell,OperateObject};PlayerActionType LastPlayerAction=PlayerActionType::None;
constexpr int GameActionType_NONE=0,DTYPE_TOWN=0,Q_INVALID=-1;
int leveltype=1,ControllerActionHeld=0,pcursmonst=-1,pcursitem=-1,pcursinvitem=-1,pcursstashitem=-1,pcurstrig=-1,pcursquest=-1;
struct StashStruct{static constexpr int EmptyCell=-1;};
struct Player{struct{Point future{10,10};}position;bool _pInvincible=false;}player;
Player *MyPlayer=&player,*PlayerUnderCursor=nullptr;
struct Monster{struct{Point future{11,10};}position;bool valid=true,talk=false;int rotation=0,steps=1;}Monsters[1];
struct Object{}object;Object *ObjectUnderCursor=nullptr;void *pcursmissile=nullptr;
Point cursPosition{};
bool DoomFlag=false,CharFlag=false,QuestLogIsOpen=false,SpellbookFlag=false,invflag=false,pointAndClick=false;
using StringOrView=std::string;std::string InfoString;
int itemSearches=0,actor=0,item=3;bool hasObject=false;
bool IsPointAndClick(){return pointAndClick;}
template<class T,class...R>bool IsNoneOf(T v,R...r){return ((v!=r)&&...);}
void InvalidateTargets(){}
void FindTrigger(){}
void FindActor(){pcursmonst=actor;}
void FindItemOrObject(){itemSearches++;pcursitem=item;if(hasObject)ObjectUnderCursor=&object;}
bool CanTargetMonster(const Monster&m){return m.valid;}
bool CanTalkToMonst(const Monster&m){return m.talk;}
int GetRotaryDistance(Point){return Monsters[0].rotation;}
int GetDistance(Point,int max){return Monsters[0].steps<=max?Monsters[0].steps:0;}
'''+fn('bool CtrHasAdjacentThreat(')+fn('void plrctrls_after_check_curs_move(')+r'''
void reset(){Monsters[0]=Monster{};itemSearches=0;leveltype=1;actor=0;item=3;hasObject=false;ControllerActionHeld=0;LastPlayerAction=PlayerActionType::None;}
void loot(){plrctrls_after_check_curs_move();check(itemSearches==1&&pcursitem==3&&pcursmonst==-1);}
int main(){
 reset();hasObject=true;plrctrls_after_check_curs_move();check(itemSearches==0&&pcursitem==-1&&ObjectUnderCursor==nullptr&&pcursmonst==0&&cursPosition==Point(11,10));
 reset();Monsters[0].valid=false;loot();
 reset();Monsters[0].talk=true;loot();
 reset();Monsters[0].rotation=2;loot();
 reset();Monsters[0].steps=0;loot(); // wall/blocked corner
 reset();Monsters[0].steps=2;loot(); // not an immediate threat
 reset();leveltype=DTYPE_TOWN;loot();
 reset();actor=-1;loot();
 reset();Monsters[0].steps=0;item=-1;hasObject=true;plrctrls_after_check_curs_move();check(itemSearches==1&&ObjectUnderCursor==&object&&pcursmonst==-1);
 reset();Monsters[0].valid=false;item=-1;plrctrls_after_check_curs_move();check(itemSearches==1&&pcursitem==-1);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'t.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(ROOT/'Source'),str(p/'t.cpp'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
print('PASS: production combat/loot focus, forward adjacent threats, walls/corners, distant/dead targets, NPCs and town interactions (ASan/UBSan)')
