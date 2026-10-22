#!/usr/bin/env python3
"""Exercise the production native HUD RGBA overlay and exact Figma XP color."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'Source/platform/ctr/display.cpp').read_text()
def function(signature):
    start=s.index(signature); opening=s.index('\n{',start)+1; end=opening+1; depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
prelude=r'''
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "platform/ctr/ui_geometry.hpp"
using namespace devilution;
enum GSPGPU_FramebufferFormat {GSP_BGR8_OES,GSP_RGB565_OES,GSP_RGBA8_OES};
struct CtrHudOverlay {const uint8_t *badges[4]{};int experiencePixels=0;bool visible=false;uint8_t opacity=255;};
void check(bool ok){if(!ok)std::abort();}
'''
code=prelude+'template <GSPGPU_FramebufferFormat Format>\n'+function('inline void StoreColor(')+'\n'+'template <GSPGPU_FramebufferFormat Format>\n'+function('void BlitHudOverlay(')+r'''
template<GSPGPU_FramebufferFormat Format> void test(){
 const int stride=Format==GSP_BGR8_OES?3:Format==GSP_RGB565_OES?2:4;
 std::vector<uint8_t> buffer(320*240*stride,64),before=buffer;
 auto pixel=[&](int x,int y){return buffer.data()+(x*240+239-y)*stride;};
 CtrHudOverlay overlay;overlay.experiencePixels=111;
 BlitHudOverlay<Format>(buffer.data(),overlay);
 std::vector<uint8_t> expected(stride);
 StoreColor<Format>(expected.data(),0x6f,0x67,0x21);
 for(int x=0;x<320;x++)for(int y=0;y<240;y++)for(int c=0;c<stride;c++){
  bool xp=x>=57&&x<57+111&&y>=169&&y<171;
  check(pixel(x,y)[c]==(xp?expected[c]:64));
 }
 std::vector<uint8_t> badge(23*23*4,0);
 // Opaque original badge pixel, including a black letter, must be visible.
 badge[0]=badge[1]=badge[2]=0;badge[3]=255;
 for(int slot=0;slot<4;slot++)overlay.badges[slot]=badge.data();
 BlitHudOverlay<Format>(buffer.data(),overlay);
 StoreColor<Format>(expected.data(),0,0,0);
 for(int slot=0;slot<4;slot++)for(int c=0;c<stride;c++)
  check(pixel(CtrHudBadgePositions[slot].x,217)[c]==expected[c]);
 overlay.opacity=0;before=buffer;
 BlitHudOverlay<Format>(buffer.data(),overlay);check(buffer==before);
 // A semi-transparent white badge pixel must blend with the live skill below.
 std::fill(buffer.begin(),buffer.end(),0);
 for(int slot=0;slot<4;slot++)overlay.badges[slot]=nullptr;
 overlay.badges[0]=badge.data();overlay.experiencePixels=0;overlay.opacity=255;
 badge[0]=badge[1]=badge[2]=255;badge[3]=128;
 BlitHudOverlay<Format>(buffer.data(),overlay);
 StoreColor<Format>(expected.data(),128,128,128);
 for(int c=0;c<stride;c++)check(pixel(73,217)[c]==expected[c]);
}
int main(){test<GSP_BGR8_OES>();test<GSP_RGB565_OES>();test<GSP_RGBA8_OES>();}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-fsanitize=address,undefined','-I'+str(ROOT/'Source'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS: native XP position/color, badge alpha/black glyphs and framebuffer edges in BGR8/RGB565/RGBA8 (ASan/UBSan)')
