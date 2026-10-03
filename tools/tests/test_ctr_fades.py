#!/usr/bin/env python3
"""Run production palette fades and scene lifetime through the native presenter."""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
ast_source=ast.parse((ROOT/'tools/tests/test_ctr_presenter.py').read_text())
STUBS=next(ast.literal_eval(n.value) for n in ast_source.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='STUBS' for t in n.targets))
STUBS['SDL.h']+='\nuint32_t SDL_GetTicks(); void SDL_Delay(int);\n'
STUBS['3ds.h']+='\nfloat osGet3DSliderState();\n'
STUBS['engine/surface.hpp']=r'''#pragma once
#include <cstdint>
#include <vector>
#include <SDL.h>
namespace devilution {
struct Surface {
    SDL_Surface *surface=nullptr;
    Surface()=default; explicit Surface(SDL_Surface *s):surface(s) {}
    int w() const { return surface->w; } int h() const { return surface->h; }
    uint8_t *at(int x,int y) const { return static_cast<uint8_t *>(surface->pixels)+surface->pitch*y+x; }
    uint8_t *begin() const { return at(0,0); } uint8_t *end() const { return at(0,h()); }
    int pitch() const { return surface->pitch; }
};
struct OwnedSurface:Surface {
    std::vector<uint8_t> data;
    SDL_PixelFormat format{8}; SDL_Surface storage{};
    OwnedSurface(int w,int h):data(w*h),storage{w,h,w,data.data(),&format} { surface=&storage; }
};
}
'''
def function(source, signature):
    start=source.index(signature); opening=source.index('\n{',start)+1
    depth=1; end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}'); end+=1
    return source[start:end]
palette=(ROOT/'Source/engine/palette.cpp').read_text()
PREFIX=r'''
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <3ds.h>
#include "platform/ctr/display.hpp"
#include "platform/ctr/world_surface.hpp"
using namespace devilution;
std::array<uint8_t,400*240*3> left{},right{},bottom{};
bool gspHasGpuRight() { return true; } void gfxSet3D(bool) {}
u8 *gfxGetFramebuffer(gfxScreen_t s,gfx3dSide_t eye,u16 *,u16 *) { return s==GFX_BOTTOM?bottom.data():eye==GFX_RIGHT?right.data():left.data(); }
GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t) { return GSP_BGR8_OES; }
void gfxFlushBuffers() {} void gspWaitForVBlank() {} void gfxScreenSwapBuffers(gfxScreen_t,bool) {}
bool C3D_FrameBegin(int) { return true; } void C3D_FrameEnd(int) {}
float osGet3DSliderState() { return 0; }
uint32_t SDL_GetTicks() { static uint32_t t=0; return t+=10; } void SDL_Delay(int) {}
namespace devilution {
bool HeadlessMode=false,sgbFadedIn=true;
namespace demo { bool IsRunning() { return false; } }
std::array<SDL_Color,256> system_palette{};
OwnedSurface canvas(640,480);
void ApplyGlobalBrightness(SDL_Color *out,const SDL_Color *src) { std::copy_n(src,256,out); }
void ApplyFadeLevel(uint32_t,SDL_Color *out,const SDL_Color *src) { std::copy_n(src,256,out); }
void SystemPaletteUpdated() { CTR_UpdateBottomPalette(system_palette.data()); }
bool IsHardwareCursor() { return false; } void ReinitializeHardwareCursor() {}
void BltFast(void *,void *) {} void RedrawEverything() {} void BlackPalette() {}
int presents=0;
void check(bool ok) { if(!ok) std::abort(); }
void RenderPresent() {
    ++presents; check(CTR_PresentFrame(canvas.surface));
    // Index 2 has an orange palette color; index 10 is the world.
    // Each palette-only frame must sample index 10, never expose index 2.
    check(left[0]==20 && left[1]==180 && left[2]==10);
}
'''
SUFFIX=r'''
}
int main() {
    system_palette[2]={200,100,50,0}; system_palette[10]={10,180,20,0};
    SystemPaletteUpdated(); CTR_ConfigureFramePresenter(true);
    CtrWorldBeginFrame(canvas,false);
    std::memset(CtrWorldSurface().at(0,0),10,640*240);
    RenderPresent(); // Consumes the initial one-shot scene.
    const int initial=presents;
    PaletteFadeIn(8,system_palette); PaletteFadeOut(8,system_palette);
    PaletteFadeIn(0,system_palette); PaletteFadeOut(0,system_palette);
    check(presents>initial+4);
    // Clearing the canvas prevents old world/right-eye data leaking into art.
    CtrWorldRepeatFrame(); CtrWorldClearFrame();
    std::fill(canvas.data.begin(),canvas.data.end(),2);
    CtrWorldRepeatFrame(); check(CTR_PresentFrame(canvas.surface));
    check(left[0]==50 && left[1]==100 && left[2]==200);
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory)
    for name,content in STUBS.items():
        p=tmp/name; p.parent.mkdir(parents=True,exist_ok=True); p.write_text(content)
    cpp=tmp/'fades.cpp'; exe=tmp/'fades'
    cpp.write_text(PREFIX+function(palette,'void PaletteFadeIn(')+function(palette,'void PaletteFadeOut(')+SUFFIX)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-DUSE_SDL1','-D__3DS__','-I'+str(tmp),'-I'+str(ROOT/'Source'),str(cpp),str(ROOT/'Source/platform/ctr/display.cpp'),str(ROOT/'Source/platform/ctr/world_surface.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: production fade frames retain the world; cleared UI never reuses it')
