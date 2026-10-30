#!/usr/bin/env python3
"""Town right-edge overscan and fresh native-world frames, without an emulator.

Runs the production tile traversal and world-buffer lifecycle. Synthetic opaque
NPC/foliage spans isolate clipping/coverage from copyrighted game data.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(signature):
    s = (ROOT / 'Source/engine/render/scrollrt.cpp').read_text()
    start = s.index(signature)
    opening = s.index('\n{', start) + 1
    end, depth = opening + 1, 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[start:end]


TRAVERSAL = r'''
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include "engine/point.hpp"
#include "platform/ctr/world_view.hpp"
using namespace devilution;
constexpr int TILE_WIDTH=64,TILE_HEIGHT=32,MAXDUNX=112,MAXDUNY=112;
int MicroTileLen=16;
struct Surface { std::array<uint8_t,400*240> pixels{}; };
struct Lightmap {};
bool InDungeonBounds(Point p) {return p.x>=0 && p.y>=0 && p.x<MAXDUNX && p.y<MAXDUNY;}
bool IsWall(Point) {return false;}
bool IsTileNotSolid(Point) {return true;}
int GetViewportWidth() {return 400;}
Point actor;
void DrawDungeon(const Surface &out, const Lightmap &,Point tile,Point target) {
    if(tile!=actor) return;
    // NPC sprites may extend left of their tile. Pixel values encode source
    // coordinates, so clipping must preserve the visible sprite's phase.
    for(int y=0;y<96;++y) for(int x=0;x<96;++x) {
        int dx=target.x-48+x,dy=target.y-y;
        if(dx>=0 && dx<400 && dy>=0 && dy<240)
            const_cast<Surface&>(out).pixels[dy*400+dx]=uint8_t(1+(x+y)%254);
    }
}
void check(bool ok) {if(!ok) {std::fprintf(stderr,"town edge regression\n");std::abort();}}
'''
TRAVERSAL_TEST = r'''
int main() {
    // Every tile/camera phase, including stationary and moving camera offsets.
    // Compare the normal traversal with a deliberately generous reference.
    for(int shift=-64;shift<=64;++shift) {
        const int start=-56+shift,columns=(400-start+63)/64;
        const Point origin{40,40};
        for(int row:{0,1,2,3}) {
            Point rowOrigin=origin;
            for(int i=0;i<row;++i) {
                if((i&1)!=0) ++rowOrigin.x; else ++rowOrigin.y;
            }
            const int rowX=start-((row&1)?32:0);
            const int rowColumns=columns+((row&1)?1:0);
            // The first excluded cell can still have visible sprite pixels.
            actor=rowOrigin+Displacement(Direction::East)*rowColumns;
            Surface reference,actual;
            DrawTileContent(reference,{},origin,{start,100},4,columns+3);
            DrawTileContent(actual,{},origin,{start,100},4,CtrWorldTileColumns(columns,true));
            check(actual.pixels==reference.pixels);
            (void)rowX;
        }
        check(CtrWorldTileColumns(columns,false)==columns); // dungeons unchanged
    }
}
'''
SURFACE_STUB = r'''#pragma once
#include <vector>
namespace devilution {
struct Surface {
    int width,height,stride; mutable std::vector<unsigned char> pixels;
    Surface(int w,int h):width(w),height(h),stride(w+8),pixels(stride*h,0xAC){}
    int w()const{return width;} int h()const{return height;} int pitch()const{return stride;}
    unsigned char* at(int x,int y)const{return pixels.data()+y*stride+x;}
    unsigned char* begin()const{return at(0,0);} unsigned char* end()const{return at(0,height);}
};
using OwnedSurface=Surface;
}
'''
DISPLAY_STUB = r'''#pragma once
namespace devilution {
struct CtrWorldFrame { const unsigned char *pixels; int pitch; const unsigned char *right; int rightPitch; };
void CTR_SetWorldFrame(const CtrWorldFrame &);
}
'''
SURFACE_TEST = r'''
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include "platform/ctr/world_surface.hpp"
#include "platform/ctr/display.hpp"
using namespace devilution;
CtrWorldFrame frame{};
void devilution::CTR_SetWorldFrame(const CtrWorldFrame &f){frame=f;}
void check(bool ok){if(!ok){std::fprintf(stderr,"world buffer regression\n");std::abort();}}
int main(){
    Surface ui(640,480);
    const Surface &world=CtrWorldSurface();
    for(int cycle=0;cycle<200;++cycle) {
        std::fill(world.pixels.begin(),world.pixels.end(),0xAC);
        std::fill(ui.pixels.begin(),ui.pixels.end(),0xBD);
        CtrWorldBeginFrame(ui);
        for(int y=0;y<240;++y) {
            for(int x=0;x<400;++x) check(*world.at(x,y)==0);
            for(int x=400;x<world.pitch();++x) check(*world.at(x,y)==0xAC);
            for(int x=0;x<640;++x) check(*ui.at(x,y)==CtrWorldKeyIndex);
            for(int x=640;x<ui.pitch();++x) check(*ui.at(x,y)==0xBD);
        }
        check(*ui.at(0,240)==0xBD);
        check(frame.pixels==world.begin() && frame.pitch==world.pitch());
        // Repeat/palette-only frames must preserve the last rendered scene.
        *world.at(399,120)=77;
        CtrWorldRepeatFrame(); check(*world.at(399,120)==77);
        CtrWorldClearFrame(); check(frame.pixels==nullptr);
        CtrWorldRepeatFrame(); check(frame.pixels==nullptr);
    }
}
'''
with tempfile.TemporaryDirectory() as folder:
    tmp=Path(folder)
    def run(name, content, extra=()):
        cpp=tmp/(name+'.cpp'); exe=tmp/name;cpp.write_text(content)
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-D__3DS__',
                        '-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                        '-I'+str(tmp),'-I'+str(ROOT/'Source'),str(cpp),*extra,'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    run('town',TRAVERSAL+function('void DrawTileContent(')+TRAVERSAL_TEST)
    (tmp/'engine').mkdir();(tmp/'platform/ctr').mkdir(parents=True)
    (tmp/'engine/surface.hpp').write_text(SURFACE_STUB)
    (tmp/'platform/ctr/display.hpp').write_text(DISPLAY_STUB)
    run('world',SURFACE_TEST,[str(ROOT/'Source/platform/ctr/world_surface.cpp')])
print('PASS: town edge traversal matches overscan reference in 129 camera phases; fresh world pixels, padding, keyed UI and repeat frames (ASan/UBSan)')
