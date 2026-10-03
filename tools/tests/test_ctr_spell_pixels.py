#!/usr/bin/env python3
"""Verify original spell artwork and four border edges through the native blit.

Decodes reference PNGs for byte comparisons; does not use an emulator.
"""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
SURFACE = r'''#pragma once
#include <cstdint>
#include <vector>
#include "engine/point.hpp"
namespace devilution {
struct Surface {
    int width, height;
    mutable std::vector<uint8_t> data;
    Surface(int w,int h):width(w),height(h),data(w*h) {}
    int w() const {return width;} int h() const {return height;}
    int pitch() const {return width;}
    uint8_t *at(int x,int y) const {return data.data()+y*width+x;}
    uint8_t &operator[](Point p) const {return *at(p.x,p.y);}
};
}
'''
def function(source, signature):
    start=source.index(signature); opening=source.index('\n{',start)+1
    depth=1; end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}'); end+=1
    return source[start:end]
BORDER=function((ROOT/'Source/engine/render/primitive_render.cpp').read_text(),'void UnsafeDrawBorder2px(')
TEST = r'''
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "engine/rectangle.hpp"
#include "platform/ctr/native_blit.hpp"
#include "platform/ctr/spell_geometry.hpp"
namespace devilution {
BORDER
}
using namespace devilution;
void check(bool ok) { if(!ok) std::abort(); }
int main(int argc,char **argv) {
    check(argc==2);
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<uint8_t> rgb{std::istreambuf_iterator<char>(file),{}};
    check(rgb.size()==56*56*3);
    Surface source(56,56),canvas(640,480);
    for(int channel=0;channel<3;++channel) {
        for(int p=0;p<56*56;++p) source.data[p]=rgb[p*3+channel];
        // Every horizontal sampling phase, including both screen edges.
        for(int origin=0;origin<=400-56;++origin) {
            std::fill(canvas.data.begin(),canvas.data.end(),0);
            CtrBlitTopNative(canvas,source,{origin,100});
            for(int y=0;y<56;++y) for(int x=0;x<56;++x)
                check(*canvas.at(CtrNativeColumn(origin+x,400),100+y)==*source.at(x,y));
        }
    }
    UnsafeDrawBorder2px(source,{{0,0},{56,56}},146);
    for(int rows=1;rows<=4;++rows) for(int row=0;row<rows;++row) for(int col=0;col<6;++col) {
        const Rectangle r=CtrSpellRect(col,row,rows);
        CtrBlitTopNative(canvas,source,r.position);
        for(int y=0;y<56;++y) for(int x=0;x<56;++x) {
            const uint8_t pixel=*canvas.at(CtrNativeColumn(r.position.x+x,400),r.position.y+y);
            check(pixel==*source.at(x,y));
            if(x<2||x>=54||y<2||y>=54) check(pixel==146);
        }
    }
    CtrBlitTopNative(canvas,source,{336,176});
    for(int y=0;y<56;++y) for(int x=0;x<56;++x)
        check(*canvas.at(CtrNativeColumn(336+x,400),176+y)==*source.at(x,y));
    // Clipping at physical edges must never access past either surface.
    CtrBlitTopNative(canvas,source,{-10,-10});
    CtrBlitTopNative(canvas,source,{390,230});
    check(*canvas.at(CtrNativeColumn(399,400),239)==*source.at(9,9));
}
'''.replace('BORDER',BORDER)
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory); stub=tmp/'engine/surface.hpp'; stub.parent.mkdir()
    stub.write_text(SURFACE)
    cpp=tmp/'pixels.cpp'; exe=tmp/'pixels'; cpp.write_text(TEST)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-I'+str(tmp),'-I'+str(ROOT/'Source'),str(cpp),'-o',str(exe)],check=True)
    for frame in ['001','026','043']:
        image=ROOT/'art/3ds/Art_Reference/Bottom_UI/spelicon_frames'/(frame+'.png')
        pixels=tmp/(frame+'.rgb')
        subprocess.run(['ffmpeg','-v','error','-i',str(image),'-f','rawvideo','-pix_fmt','rgb24',str(pixels)],check=True)
        subprocess.run([str(exe),str(pixels)],check=True)
print('PASS: three original 56x56 spell frames unchanged at every position; all four 2px border edges survive; clipping checked with sanitizers')
