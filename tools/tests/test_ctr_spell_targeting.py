#!/usr/bin/env python3
"""Run actual 3DS spell targeting and the shared four-slot cast entry point."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'Source/controls/plrctrls.cpp').read_text()
def fn(sig):
 a=s.index(sig);b=s.index('{',a);c=b+1;d=1
 while d:d+=(s[c]=='{')-(s[c]=='}');c+=1
 return s[a:c]
pre=r'''
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstddef>
#include "engine/point.hpp"
using namespace devilution;
#define check(ok) do {if(!(ok)){std::fprintf(stderr,"failed line %d\n",__LINE__);std::abort();}} while(0)
enum class SpellID{Invalid,Firebolt,TrapDisarm,Teleport,Resurrect,HealOther,Healing,Golem,FireWall,RuneOfFire,RuneOfLight,RuneOfNova,RuneOfImmolation,RuneOfStone};
bool IsValidSpell(SpellID s){return s!=SpellID::Invalid;}
struct SpellData{SpellID s;bool isTargeted()const{return s!=SpellID::Healing;}bool isAllowedInTown()const{return s==SpellID::Healing||s==SpellID::Teleport;}};
SpellData GetSpellData(SpellID s){return {s};}
struct Item{bool isEmpty()const{return true;}};
struct Player{struct{Point future{10,10};}position;Item HoldItem;bool plractive=false,activeLevel=true,dead=false;Direction _pdir=Direction::South;bool isOnActiveLevel()const{return activeLevel;}bool hasNoLife()const{return dead;}} player;
Player* MyPlayer=&player,*PlayerUnderCursor=nullptr;
struct Monster{struct{Point future;}position;bool valid=true,talk=false,blocked=false;};
Monster Monsters[8];int ActiveMonsters[8];size_t ActiveMonsterCount=0;
struct Object{Point position;bool _oTrapFlag=false,disabled=false,interact=true;bool canInteractWith()const{return interact;}bool IsDisabled()const{return disabled;}};
Object Objects[8];int ActiveObjects[8],ActiveObjectCount=0;
Object *ObjectUnderCursor=nullptr;int pcursmonst=-1;Point cursPosition{};
bool CanTargetMonster(const Monster&m,bool facing){check(!facing);return m.valid;}
bool CanTalkToMonst(const Monster&m){return m.talk;}
bool IsTileLit(Point p){return p.x>=0&&p.y>=0;}
bool LineClearMissile(Point,Point p){for(const auto&m:Monsters)if(m.position.future==p&&m.blocked)return false;return true;}
int GetRotaryDistance(Point p){return p.x<10?4:0;}
bool MyPlayerIsDead=false,invflag=false,CharFlag=false,SpellbookFlag=false,QuestLogIsOpen=false,qtextflag=false,IsStashOpen=false,menu=false,store=false;
int PauseMode=0;
bool gmenu_is_active(){return menu;}bool IsPlayerInStore(){return store;}
enum class ControlTypes{KeyboardAndMouse,Gamepad};ControlTypes ControlMode=ControlTypes::KeyboardAndMouse;
enum class PlayerActionType{None,Spell};PlayerActionType LastPlayerAction=PlayerActionType::None;
int primary=0,quick=0,last=-1;
void PerformSpellAction(){primary++;LastPlayerAction=PlayerActionType::Spell;}
bool IsWallSpell(SpellID s){return s==SpellID::FireWall;}
bool SpellHasActorTarget(SpellID){return pcursmonst!=-1||PlayerUnderCursor!=nullptr;}
bool IsValidSpeedSpell(size_t slot){return slot!=2;}
void QuickCast(size_t slot){quick++;last=slot;}
'''
missiles=(ROOT/'Source/missiles.cpp').read_text()
a=missiles.index('void AddTrapDisarm(');b=missiles.index('{',a);c=b+1;depth=1
while depth:depth+=(missiles[c]=='{')-(missiles[c]=='}');c+=1
trap=missiles[a:c]
pre+=r'''
#define __3DS__ 1
Player Players[2];
struct Missile{int _misource=0;bool _miDelFlag=false;};
struct AddMissileParameter{Point dst;};
constexpr int MyPlayerId=0,CMD_DISARMXY=1,CURSOR_HAND=0,CURSOR_HEALOTHER=2,CURSOR_RESURRECT=3;
int cursor=0,heals=0,resurrects=0;
int disarms=0;Point disarmed;
Object*FindObjectAtPosition(Point p){for(auto&o:Objects)if(o.position==p)return &o;return nullptr;}
void NetSendCmdLoc(int,bool,int,Point p){disarms++;disarmed=p;}
void NewCursor(int value){cursor=value;}
bool TryIconCurs(){if(!PlayerUnderCursor)return false;if(cursor==CURSOR_HEALOTHER){check(!PlayerUnderCursor->hasNoLife());heals++;}else if(cursor==CURSOR_RESURRECT){check(PlayerUnderCursor->hasNoLife());resurrects++;}else return false;return true;}
'''
def missile_fn(sig):
 a=missiles.index(sig);b=missiles.index('{',a);c=b+1;d=1
 while d:d+=(missiles[c]=='{')-(missiles[c]=='}');c+=1
 return missiles[a:c]
code=pre+fn('bool SelectCtrSpellTarget(')+fn('void CastCtrSpellShortcut(')+fn('void UpdateSpellTarget(')+trap+missile_fn('void AddHealOther(')+missile_fn('void AddResurrect(')+r'''
int main(){
 ActiveMonsterCount=4;for(int i=0;i<4;i++)ActiveMonsters[i]=i;
 Monsters[0].position.future={14,10};Monsters[1].position.future={9,10};Monsters[2].position.future={12,10};Monsters[3].position.future={11,10};Monsters[3].blocked=true;
 check(SelectCtrSpellTarget(SpellID::Firebolt)&&pcursmonst==1); // closer enemy behind beats front enemy
 Monsters[1].valid=false;check(SelectCtrSpellTarget(SpellID::Firebolt)&&pcursmonst==2);
 Monsters[2].talk=true;check(SelectCtrSpellTarget(SpellID::Firebolt)&&pcursmonst==0);
 Monsters[0].position.future={30,10};check(!SelectCtrSpellTarget(SpellID::Firebolt)&&pcursmonst==-1);
 pcursmonst=0;check(!SelectCtrSpellTarget(SpellID::Teleport)&&pcursmonst==0);check(!SelectCtrSpellTarget(SpellID::Healing));
 for(auto spell:{SpellID::Teleport,SpellID::Golem,SpellID::FireWall,SpellID::RuneOfFire}) {
  pcursmonst=0;PlayerUnderCursor=&Players[1];UpdateSpellTarget(spell);
  check(pcursmonst==-1&&PlayerUnderCursor==nullptr&&cursPosition==player.position.future+Displacement(player._pdir)*(spell==SpellID::Teleport?4:1));
 }
 MyPlayer=&Players[0];Players[1].position.future={10,11};Players[1].plractive=true;
 check(SelectCtrSpellTarget(SpellID::HealOther)&&PlayerUnderCursor==&Players[1]);
 check(SelectCtrSpellTarget(SpellID::Resurrect)&&PlayerUnderCursor==nullptr);
 Players[1].dead=true;check(SelectCtrSpellTarget(SpellID::Resurrect)&&PlayerUnderCursor==&Players[1]);
 check(SelectCtrSpellTarget(SpellID::HealOther)&&PlayerUnderCursor==nullptr);
 Players[1].activeLevel=false;check(SelectCtrSpellTarget(SpellID::Resurrect)&&PlayerUnderCursor==nullptr);
 MyPlayer=&player;
 ActiveObjectCount=3;for(int i=0;i<3;i++)ActiveObjects[i]=i;
 Objects[0]={{11,10},false};Objects[1]={{12,10},true};Objects[2]={{9,10},true};
 check(SelectCtrSpellTarget(SpellID::TrapDisarm)&&ObjectUnderCursor==&Objects[2]&&pcursmonst==-1&&cursPosition==Point(9,10));
 Objects[2].disabled=true;check(SelectCtrSpellTarget(SpellID::TrapDisarm)&&ObjectUnderCursor==&Objects[1]);
 Objects[1]._oTrapFlag=false;check(SelectCtrSpellTarget(SpellID::TrapDisarm)&&ObjectUnderCursor==nullptr);
 for(int i=0;i<4;i++){ControlMode=ControlTypes::KeyboardAndMouse;CastCtrSpellShortcut(i);check(ControlMode==ControlTypes::Gamepad);}
 check(primary==1&&quick==2&&last==3&&LastPlayerAction==PlayerActionType::None);
 CastCtrSpellShortcut(4);check(primary==1&&quick==2);
 invflag=true;CastCtrSpellShortcut(0);check(primary==1);invflag=false;menu=true;CastCtrSpellShortcut(1);check(quick==2);
 MyPlayer=&Players[0];Objects[1]._oTrapFlag=true;ObjectUnderCursor=nullptr;cursPosition={50,50};
 Missile missile;AddMissileParameter parameter{{12,10}};AddTrapDisarm(missile,parameter);
 check(missile._miDelFlag&&disarms==1&&disarmed==Point(12,10));
 Objects[1]._oTrapFlag=false;AddTrapDisarm(missile,parameter);check(disarms==1);
 missile._misource=1;Objects[1]._oTrapFlag=true;AddTrapDisarm(missile,parameter);check(disarms==1);
 // Animation completion must re-resolve the ally even if the frame focus changed.
 missile._misource=0;ControlMode=ControlTypes::Gamepad;
 Players[1].activeLevel=true;Players[1].dead=false;PlayerUnderCursor=nullptr;pcursmonst=0;
 AddHealOther(missile,parameter);check(heals==1&&resurrects==0&&cursor==CURSOR_HAND&&PlayerUnderCursor==&Players[1]);
 Players[1].dead=true;PlayerUnderCursor=nullptr;AddResurrect(missile,parameter);check(resurrects==1&&cursor==CURSOR_HAND);
 Players[1].plractive=false;PlayerUnderCursor=&Players[1];AddHealOther(missile,parameter);check(heals==1&&cursor==CURSOR_HAND&&PlayerUnderCursor==nullptr);
 missile._misource=1;AddResurrect(missile,parameter);check(resurrects==1);

}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'t.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(ROOT/'Source'),str(p/'t.cpp'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
print('PASS: nearest enemy in all directions, invalid/blocked/talking targets, trapped objects, ground placement, allies through animation completion and four single-cast slots (ASan/UBSan)')
