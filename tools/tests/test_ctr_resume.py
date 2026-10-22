#!/usr/bin/env python3
"""Production APT hooks defer recovery; waking never finishes a held gesture."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
def fn(file,sig):
 s=(ROOT/file).read_text();a=s.index(sig);b=s.index('{',a);c=b+1;d=1
 while d:d+=(s[c]=='{')-(s[c]=='}');c+=1
 return s[a:c]
code=r'''
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include "controls/controller_buttons.h"
#define check(v) do{if(!(v)){std::fprintf(stderr,"line %d\n",__LINE__);std::abort();}}while(0)
using Uint32=uint32_t;
uint32_t now=0;uint32_t SDL_GetTicks(){return now;}
namespace devilution {
enum class PlayerActionType{None,Spell};enum{GameActionType_NONE,GameActionType_CAST_SPELL,CLICK_NONE,CLICK_LEFT};
struct Player{int stops=0;void Stop(){stops++;}}player;Player *MyPlayer=&player;
bool gbRunGame=true,gbIsMultiplayer=false,PadHotspellMenuActive=true,PadMenuNavigatorActive=true,HudTouchDown=true;
int last_tick=-1,ControllerActionHeld=GameActionType_CAST_SPELL,sgbMouseDown=CLICK_LEFT;
PlayerActionType LastPlayerAction=PlayerActionType::Spell;
ControllerButton SuppressedButton=ControllerButton_BUTTON_LEFTSHOULDER;
uint32_t CtrConsumedSpellButtons=42;
struct{bool active=true,triggered=false;}ButtonAHold;
struct{bool active=true,finished=false;}StashItemButton;
struct{int cancels=0;void Cancel(){cancels++;}}HudBeltGesture;
float leftStickX=1,leftStickY=1,rightStickX=1,rightStickY=1,leftStickXUnscaled=1,leftStickYUnscaled=1,rightStickXUnscaled=1,rightStickYUnscaled=1;
bool leftStickNeedsScaling=false,rightStickNeedsScaling=false;
int releaseCalls=0,panels=0,redraws=0,logs=0,cancelSale=0;
void Cancel3DSButtonAHold(){ButtonAHold.active=ButtonAHold.triggered=false;}
void Confirm3DSVisualStoreSale(bool yes){check(!yes);cancelSale++;}
void PadmapperRelease(ControllerButton,bool invoke){check(!invoke);releaseCalls++;}
void ResetMainPanelButtons(){panels++;}void RedrawEverything(){redraws++;}
void LogInfo(const char*,unsigned){logs++;}
bool nativePresenterSynchronized=true,worldFramePending=true;struct WorldFrame{const void*p=nullptr;}worldFrame{&player};
'''+fn('Source/controls/game_controls.cpp','void Reset3DSInputAfterResume(')+fn('Source/platform/ctr/display.cpp','void CTR_InvalidateFramePresenter(')+fn('Source/controls/plrctrls.cpp','struct RightStickAccumulator {')+';\n}\n'+r'''
using namespace devilution;
std::atomic_bool ResumePending{false};std::atomic_uint ResumeCount{0};
enum APT_HookType{APTHOOK_ONSUSPEND,APTHOOK_ONSLEEP,APTHOOK_ONRESTORE,APTHOOK_ONWAKEUP,APTHOOK_ONEXIT};
void ctr_lcd_backlight_on(){}void ctr_lcd_backlight_off(){}
'''+fn('Source/platform/ctr/system.cpp','void ctr_process_resume(')+fn('Source/platform/ctr/system.cpp','void aptHookFunc(')+r'''
int main(){
 ctr_process_resume();check(releaseCalls==0);
 aptHookFunc(APTHOOK_ONSLEEP,nullptr);check(releaseCalls==0);
 for(unsigned i=1;i<=1000;i++) {
  ButtonAHold.active=true;StashItemButton.active=true;HudTouchDown=true;
  nativePresenterSynchronized=true;worldFramePending=true;worldFrame.p=&player;
  now=i*1000000u;
  aptHookFunc(APTHOOK_ONWAKEUP,nullptr);
  check(ButtonAHold.active&&HudTouchDown); // no game/audio work inside SDK hook
  ctr_process_resume();
  check(!ButtonAHold.active&&!StashItemButton.active&&StashItemButton.finished&&!HudTouchDown);
  check(ControllerActionHeld==GameActionType_NONE&&LastPlayerAction==PlayerActionType::None&&sgbMouseDown==CLICK_NONE);
  check(!nativePresenterSynchronized&&!worldFramePending&&worldFrame.p==nullptr);
  check(leftStickX==0&&rightStickX==0&&leftStickXUnscaled==0&&rightStickXUnscaled==0);
  check(last_tick==static_cast<int>(now)&&player.stops==int(i)&&logs==int(i));
  const int calls=releaseCalls;ctr_process_resume();check(releaseCalls==calls);
 }
 gbIsMultiplayer=true;last_tick=123;aptHookFunc(APTHOOK_ONRESTORE,nullptr);ctr_process_resume();check(last_tick==123&&player.stops==1000);
 MyPlayer=nullptr;gbRunGame=false;aptHookFunc(APTHOOK_ONRESTORE,nullptr);ctr_process_resume();check(last_tick==123);
 now=0;RightStickAccumulator acc;rightStickX=1;int x=0,y=0;now=600000;acc.Pool(&x,&y,10);check(x==10&&y==0); // 10 minutes cannot overflow/move cursor offscreen
 now=UINT32_MAX-20;RightStickAccumulator wrap;now=20;x=0;wrap.Pool(&x,&y,1);check(x==41);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'t.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(ROOT/'Source'),str(p/'t.cpp'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
print('PASS: 1000 production wake cycles, deferred hooks, no gesture release actions, GPU invalidation, multiplayer clock preservation, bounded pointer and tick wrap (ASan/UBSan)')
