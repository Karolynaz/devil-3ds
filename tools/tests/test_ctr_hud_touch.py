#!/usr/bin/env python3
"""Execute real HUD touch handlers: casting, hold/use, swaps and safe cancellation."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'Source/controls/game_controls.cpp').read_text()
def fn(signature):
 a=s.index(signature);b=s.index('{',a);d=1;c=b+1
 while d:d+=(s[c]=='{')-(s[c]=='}');c+=1
 return s[a:c]
pre=r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "platform/ctr/ui_geometry.hpp"
#include "platform/ctr/belt_gesture.hpp"
using namespace devilution;
using Uint32=uint32_t;using Uint8=uint8_t;
constexpr int SDL_EVENT_MOUSE_BUTTON_DOWN=1,SDL_EVENT_MOUSE_MOTION=2,SDL_EVENT_MOUSE_BUTTON_UP=3,SDL_BUTTON_LEFT=1;
constexpr int MaxBeltItems=8,CMD_DELBELTITEMS=10;
struct Item {int _iSeed=0,IDidx=0,_iCreateInfo=0,_iMiscId=0,_iSpell=0;bool isEmpty()const{return IDidx==0;}bool isScroll()const{return _iMiscId==2;}};
struct Player{Item SpdList[8],HoldItem;void CalcScrolls(){}}player;
Player *MyPlayer=&player;
bool MyPlayerIsDead=false,invflag=false,CharFlag=false,SpellbookFlag=false,QuestLogIsOpen=false;
bool IsStashOpen=false,qtextflag=false,ChatFlag=false,DropGoldFlag=false,SpellSelectFlag=false,menu=false,store=false;
int PauseMode=0,gnScreenWidth=640;
bool gmenu_is_active(){return menu;}bool IsPlayerInStore(){return store;}
Point MousePosition;uint32_t now=0;uint32_t SDL_GetTicks(){return now;}
enum class ControlTypes{KeyboardAndMouse,Gamepad};ControlTypes ControlMode=ControlTypes::KeyboardAndMouse;
CtrBeltGesture HudBeltGesture;Item HudBeltOriginal;bool HudTouchDown=false;
int casts=0,used=0,targets=0,last=-1;std::vector<int> changed,removed;
void CastCtrSpellShortcut(int slot){casts++;last=slot;}
void UseBeltSlot(int slot){used++;last=slot;player.SpdList[slot]={};}
void UpdateSpellTarget(int){targets++;}
void RedrawEverything(){}
void NetSendCmdChBeltItem(bool,int slot){changed.push_back(slot);}
void NetSendCmdParam1(bool,int,int slot){removed.push_back(slot);}
void check(bool v){if(!v)std::abort();}
void point(Point p){MousePosition={p.x*2,p.y+240};}
'''
code=pre+'\n'.join(fn(x) for x in ['bool CtrHudTouchAllowed()', 'Point CtrHudTouchPoint()', 'int CtrHudBeltHit(', 'bool CtrHudBeltUnchanged()', 'void Update3DSHudTouch()', 'int CtrHudDraggedBeltSlot()', 'bool Handle3DSHudTouch('])+r'''
void down(int slot){point(CtrBottomBeltSlot(slot).Center());check(Handle3DSHudTouch(1,1));}
void up(){check(Handle3DSHudTouch(3,1));}
int main(){
 for(int i=0;i<4;i++){point(CtrHudSpellSlot(i).Center());check(Handle3DSHudTouch(1,1));up();check(casts==i+1&&last==i);}
 player.SpdList[0]={1,1};down(0);now+=449;Update3DSHudTouch();check(used==0);up();check(used==0&&!player.SpdList[0].isEmpty());
 down(0);now+=450;Update3DSHudTouch();check(used==1&&last==0);now+=999;Update3DSHudTouch();up();check(used==1);
 player.SpdList[0]={10,1};player.SpdList[1]={11,2};
 down(0);point(CtrBottomBeltSlot(1).Center());Handle3DSHudTouch(2,0);check(CtrHudDraggedBeltSlot()==0);now+=1000;Update3DSHudTouch();check(used==1);up();
 check(player.SpdList[0]._iSeed==11&&player.SpdList[1]._iSeed==10);check(changed.size()==2);
 down(1);point(CtrBottomBeltSlot(7).Center());Handle3DSHudTouch(2,0);up();check(player.SpdList[1].isEmpty()&&player.SpdList[7]._iSeed==10);check(removed.back()==1);
 down(7);point({0,150});Handle3DSHudTouch(2,0);up();check(player.SpdList[7]._iSeed==10);
 down(7);player.SpdList[7]._iSeed=99;now+=500;Update3DSHudTouch();up();check(used==1&&player.SpdList[7]._iSeed==99);
 player.SpdList[2]={20,3,0,2,42};down(2);now+=450;up();check(used==2&&targets==1);
 invflag=true;point(CtrBottomBeltSlot(0).Center());check(!Handle3DSHudTouch(1,1));invflag=false;
 down(0);menu=true;now+=1000;Update3DSHudTouch();up();check(used==2);menu=false;
 // Clock wrap, tremor tolerance and a drag that returns to origin.
 CtrBeltGesture g;g.Begin(0,{20,20},UINT32_MAX-200);check(!g.Update({22,21},100));check(g.Update({20,20},300));check(!g.Update({20,20},900));
 g.Begin(0,{0,0},0);check(!g.Update({5,0},10)&&g.dragging);check(!g.Update({0,0},1000));
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'t.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(ROOT/'Source'),str(p/'t.cpp'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
print('PASS: four touch casts, one-shot hold, scroll targeting, occupied/empty belt swaps, cancel, item mutation and timer wrap (ASan/UBSan)')
