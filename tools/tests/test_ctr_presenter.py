#!/usr/bin/env python3
"""Execute the production presenter with host mocks for the 3DS framebuffer API.

Checks indexed scene/UI composition, both eyes, native edge pixels, movie-only
frames, GPU suspension and recovery. No emulator or hardware is implied.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
STUBS = {
    'SDL.h': '''#pragma once
#include <cstdint>
struct SDL_Color { uint8_t r, g, b, unused; };
struct SDL_PixelFormat { uint8_t BitsPerPixel; };
struct SDL_Surface { int w, h, pitch; void *pixels; SDL_PixelFormat *format; };
''',
    '3ds.h': '''#pragma once
#include <cstdint>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
enum GSPGPU_FramebufferFormat { GSP_RGBA8_OES, GSP_BGR8_OES, GSP_RGB565_OES };
enum gfxScreen_t { GFX_TOP, GFX_BOTTOM };
enum gfx3dSide_t { GFX_LEFT, GFX_RIGHT };
bool gspHasGpuRight();
void gfxSet3D(bool);
u8 *gfxGetFramebuffer(gfxScreen_t, gfx3dSide_t, u16 *, u16 *);
GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t);
void gfxFlushBuffers();
void gspWaitForVBlank();
void gfxScreenSwapBuffers(gfxScreen_t, bool);
''',
    'citro3d.h': '''#pragma once
constexpr int C3D_FRAME_SYNCDRAW=1, GX_CMDLIST_FLUSH=2;
bool C3D_FrameBegin(int);
void C3D_FrameEnd(int);
''',
}
TEST = r'''
#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>
#include <3ds.h>
#include "platform/ctr/display.hpp"
#include "platform/ctr/world_view.hpp"
#include "platform/ctr/pixel_geometry.hpp"
using namespace devilution;
bool gpu=true, failBegin=false, stereo=false;
int beginCalls=0, swaps=0;
GSPGPU_FramebufferFormat format=GSP_BGR8_OES;
std::array<uint8_t, 400*240*4> left{}, right{}, bottom{};
bool gspHasGpuRight() { return gpu; }
void gfxSet3D(bool value) { stereo=value; }
u8 *gfxGetFramebuffer(gfxScreen_t s, gfx3dSide_t eye, u16 *w, u16 *h) {
    if (w) *w=240;
    if (h) *h=s==GFX_TOP?400:320;
    return s==GFX_BOTTOM ? bottom.data() : eye==GFX_RIGHT?right.data():left.data();
}
GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t) { return format; }
void gfxFlushBuffers() {}
void gspWaitForVBlank() {}
void gfxScreenSwapBuffers(gfxScreen_t, bool) { ++swaps; }
bool C3D_FrameBegin(int flags) {
    ++beginCalls;
    if (flags!=1) std::abort();
    return !failBegin;
}
void C3D_FrameEnd(int) {}
void check(bool ok) { if (!ok) std::abort(); }
size_t pixel(int x, int y, int bytes=3) { return (x*240+239-y)*bytes; }
void checkColor(const std::array<uint8_t,400*240*4> &fb, int x, int y, uint8_t index) {
    const auto p=pixel(x,y);
    check(fb[p]==index && fb[p+1]==index && fb[p+2]==index);
}
int main() {
    std::array<SDL_Color,256> palette{};
    for (int i=0;i<256;++i) palette[i]={uint8_t(i),uint8_t(i),uint8_t(i),0};
    CTR_UpdateBottomPalette(palette.data());
    // Pitch padding catches accidental width-as-pitch access.
    constexpr int pitch=648;
    std::vector<uint8_t> ui(pitch*480,0), scene(pitch*240), other(pitch*240);
    for (int y=0;y<240;++y) for (int x=0;x<640;++x) {
        scene[y*pitch+x]=uint8_t(20+(x+y)%200);
        other[y*pitch+x]=uint8_t(21+(x+y)%200);
        ui[y*pitch+x]=CtrWorldKeyIndex;
    }
    SDL_PixelFormat pf{8};
    SDL_Surface surface{640,480,pitch,ui.data(),&pf};
    CTR_ConfigureFramePresenter(true);
    // Transient startup failure is consumed, not handed to SDL_Flip.
    failBegin=true;
    check(CTR_PresentFrame(&surface)); check(swaps==0 && beginCalls==1);
    failBegin=false;
    CTR_SetWorldFrame({scene.data(),pitch,nullptr,0});
    check(CTR_PresentFrame(&surface)); check(beginCalls==2 && swaps==2 && !stereo);
    for (int y=0;y<240;++y) for (int x=0;x<400;++x)
        checkColor(left,x,y,scene[y*pitch+CtrNativeColumn(x,400)]);
    // UI survives at the rightmost column; both eyes share the same UI.
    ui[50*pitch+638]=240;
    ui[70*pitch+320]=CtrWorldDimKeyIndex;
    CTR_SetWorldFrame({scene.data(),pitch,other.data(),pitch});
    check(CTR_PresentFrame(&surface)); check(stereo);
    checkColor(left,399,50,240); checkColor(right,399,50,240);
    checkColor(left,200,70,scene[70*pitch+320]/2);
    checkColor(right,200,70,other[70*pitch+320]/2);
    checkColor(right,0,0,other[0]);
    // A movie/plain UI frame cannot reuse the previous scene or right eye.
    std::fill(ui.begin(),ui.end(),0);
    ui[100*pitch+320]=150;
    check(CTR_PresentFrame(&surface)); check(!stereo);
    checkColor(left,200,100,150); checkColor(left,0,0,0);
    for (int y=0;y<240;++y) for (int x=0;x<320;++x) checkColor(bottom,x,y,0);
    // Native 320-wide glyph strokes never interpolate into neighboring pixels.
    ui[300*pitch+638]=200; ui[300*pitch+639]=200;
    check(CTR_PresentFrame(&surface)); checkColor(bottom,319,60,200);
    checkColor(bottom,318,60,0);
    const int savedSwaps=swaps;
    gpu=false; check(CTR_PresentFrame(&surface)); check(swaps==savedSwaps);
    gpu=true; check(CTR_PresentFrame(&surface)); check(swaps==savedSwaps+2);
    // All formats used by the SDL port remain supported.
    for (auto f : {GSP_RGB565_OES,GSP_RGBA8_OES}) {
        format=f; check(CTR_PresentFrame(&surface));
        if (f==GSP_RGBA8_OES) {
            const auto p=pixel(200,100,4);
            check(left[p]==255 && left[p+1]==150 && left[p+2]==150 && left[p+3]==150);
        } else {
            uint16_t value; std::memcpy(&value,left.data()+pixel(200,100,2),2);
            check(value==((150>>3)<<11 | (150>>2)<<5 | (150>>3)));
        }
    }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    folder = Path(tmp)
    for name, contents in STUBS.items():
        (folder/name).write_text(contents)
    (folder/'test.cpp').write_text('#include <cstring>\n'+TEST)
    executable = folder/'presenter'
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++17', '-Wall', '-Wextra',
                    '-Werror', '-Wno-unused-parameter', '-I'+str(folder),
                    '-I'+str(ROOT/'Source'), str(ROOT/'Source/platform/ctr/display.cpp'),
                    str(folder/'test.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print('PASS: production presenter, scene/UI composition, stereo, movie routing, pixel edges, GPU recovery')
