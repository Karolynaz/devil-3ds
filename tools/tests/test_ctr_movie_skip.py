#!/usr/bin/env python3
"""Replay actual movie input loop to reject the hero-confirmation release."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'Source/movie.cpp').read_text();a=s.index('void play_movie(');b=s.index('\n{',a)+1;c=b+1;depth=1
while depth:depth+=(s[c]=='{')-(s[c]=='}');c+=1
fn=s[a:c]
pre=r'''
#include <cstdint>
#include <cstdlib>
#include <vector>
#define __3DS__ 1
#define USE_SDL1 1
constexpr int SDL_EVENT_KEY_DOWN=1,SDL_EVENT_MOUSE_BUTTON_UP=2,SDL_ACTIVEEVENT=3,SDL_EVENT_QUIT=4,JOYSTICK=5;
constexpr int SDL_APPINPUTFOCUS=1,SDLK_ESCAPE=27,ControllerButton_BUTTON_A=1,ControllerButton_BUTTON_B=2,ControllerButton_BUTTON_START=3,ControllerButton_BUTTON_BACK=4;
struct SDL_Event{int type=0;struct{int state=0,gain=1;}active;int button=1;bool up=false;int key=0;};
struct ControllerButtonEvent{int button;bool up;};
struct Point{int x=0,y=0;}MousePosition;
struct Options{struct{bool value=false;bool *pauseOnFocusLoss=&value;}Gameplay;}options;
Options &GetOptions(){return options;}
namespace demo{bool IsRunning(){return false;}}
enum class ControlTypes{KeyboardAndMouse,Gamepad};ControlTypes ControlDevice=ControlTypes::Gamepad;
bool HeadlessMode=false,movie_playing=false,loop_movie=false,held=true;
int frame=0,ended=0,scenario=0,delivered=-1;
bool IsHardwareCursorEnabled(){return false;}
void SetHardwareCursorVisible(bool){}void sound_disable_music(bool){}void stream_stop(){}
bool SVidPlayBegin(const char*,int){return true;}
bool SVidPlayContinue(){frame++;return frame<8;}
void SVidPlayEnd(){ended++;}
bool IsControllerButtonPressed(int key){return key==1&&held;}
unsigned SDL_GetMouseState(int*x,int*y){if(x)*x=0;if(y)*y=0;return 0;}
std::vector<ControllerButtonEvent> ToControllerButtonEvents(const SDL_Event&e){if(e.type==JOYSTICK)return {{e.button,e.up}};return {};}
bool SkipsMovie(ControllerButtonEvent e){return e.button>=1&&e.button<=4;}
int SDLC_EventKey(const SDL_Event&e){return e.key;}
void diablo_focus_pause(){}void diablo_focus_unpause(){}void diablo_quit(int){std::abort();}
void OutputToLogical(int*,int*){}void InitBackbufferState(){}
bool FetchMessage(SDL_Event*e,uint16_t*){
 if(delivered==frame)return false;
 if(frame==0){delivered=frame;held=false;*e={JOYSTICK};e->up=true;return true;}
 if(frame==3&&scenario==1){delivered=frame;held=true;*e={JOYSTICK};return true;}
 if(frame==3&&scenario==2){delivered=frame;*e={SDL_EVENT_MOUSE_BUTTON_UP};return true;}
 if(frame==4&&scenario==3){delivered=frame;*e={SDL_EVENT_KEY_DOWN};e->key=SDLK_ESCAPE;return true;}
 return false;
}
void check(bool ok){if(!ok)std::abort();}
'''
code=pre+fn+r'''
int main(){for(scenario=0;scenario<4;scenario++){
 frame=0;ended=0;delivered=-1;held=true;play_movie("Hellfire.smk",true);
 check(ended==1&&!movie_playing);
 if(scenario==0||scenario==2)check(frame==8); // release and touch-up never skip
 if(scenario==1)check(frame==4); // new press after release does skip
 if(scenario==3)check(frame==5); // intentional Escape still works
}}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'t.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
print('PASS: actual movie loop ignores held-confirm release/touch-up, completes naturally, permits fresh skip/escape (ASan/UBSan)')
