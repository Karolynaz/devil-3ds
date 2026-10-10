#!/usr/bin/env python3
"""Exercise production frame timing and the native/fallback present routes."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
source = (ROOT/'Source/engine/dx.cpp').read_text()
def function(signature):
    start=source.index(signature);opening=source.index('\n{',start)+1
    end=opening+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
prelude=r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <vector>
uint32_t ticks=1000;
std::vector<uint32_t> sleeps;
uint32_t SDL_GetTicks(){return ticks;}
void SDL_Delay(uint32_t ms){sleeps.push_back(ms);ticks+=ms;}
enum class FrameRateControl {CPUSleep,None,VerticalSync};
FrameRateControl mode=FrameRateControl::CPUSleep;
struct Options {struct {FrameRateControl *frameRateControl=&mode;} Graphics;} options;
Options &GetOptions(){return options;}
int refreshDelay=16666;
bool movie_playing=false,HeadlessMode=false,gbActive=true,RenderDirectlyToOutputSurface=false;
struct SDL_Surface{} surface;
SDL_Surface *PalSurface=&surface;
SDL_Surface *GetOutputSurface(){return &surface;}
bool native=true;
int nativeCalls=0,flips=0;
bool CTR_PresentFrame(const SDL_Surface*){++nativeCalls;return native;}
int SDL_Flip(SDL_Surface*){++flips;return 0;}
void ErrSdl(){std::abort();}
'''
test=r'''
int main(){
 LimitFrameRate();assert(sleeps.empty());
 ticks=1007;LimitFrameRate();assert(sleeps.back()==10 && ticks==1017);
 ticks=1021;LimitFrameRate();assert(sleeps.back()==13 && ticks==1034);
 auto count=sleeps.size();ticks=1100;LimitFrameRate();assert(sleeps.size()==count);
 ticks=1104;LimitFrameRate();assert(sleeps.back()==13);
 // SDL tick rollover must reset the deadline rather than sleep for days.
 ticks=4;count=sleeps.size();LimitFrameRate();assert(sleeps.size()==count);
 movie_playing=true;LimitFrameRate();assert(sleeps.size()==count);movie_playing=false;
 refreshDelay=0;LimitFrameRate();assert(sleeps.size()==count);refreshDelay=16666;
 mode=FrameRateControl::None;LimitFrameRate();assert(sleeps.size()==count);mode=FrameRateControl::CPUSleep;
 // Native presentation waits for VBlank itself, with no CPU sleep afterward.
 for(int i=0;i<5;++i)RenderPresent();assert(nativeCalls==5 && flips==0 && sleeps.size()==count);
 native=false;ticks=2000;RenderPresent();assert(flips==1 && sleeps.size()==count);
 ticks=2003;RenderPresent();assert(flips==2 && sleeps.back()==14);
 HeadlessMode=true;count=sleeps.size();RenderPresent();assert(flips==2 && sleeps.size()==count);
}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.cpp').write_text(prelude+function('void LimitFrameRate(')+function('void RenderPresent(')+test)
 subprocess.run(['c++','-std=c++17','-D__3DS__','-DUSE_SDL1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: exact remaining-deadline waits, fractional timing, slow frames, tick rollover, native VBlank bypass and fallback limiter (ASan/UBSan)')
