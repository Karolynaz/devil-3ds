#!/usr/bin/env python3
"""Run production map bounds and pixel clipping without a console or emulator."""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
def extract(path, signature):
    s=(ROOT/path).read_text(); start=s.index(signature); opening=s.index('\n{',start)+1; end=opening+1; depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
CPP=r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include "engine/rectangle.hpp"
using namespace devilution;
enum class AutomapType { Opaque, Transparent, Minimap };
AutomapType mode=AutomapType::Minimap;
AutomapType GetAutomapType() { return mode; }
int GetViewportWidth() { return 640; }
Rectangle MinimapRect;
struct Surface {
    mutable std::vector<uint8_t> pixels=std::vector<uint8_t>(640*240, 7);
    void SetPixel(Point p, uint8_t c) const { assert(p.x>=0 && p.x<640 && p.y>=0 && p.y<240);pixels[p.y*640+p.x]=c; }
};
void SetHalfTransparentPixel(const Surface &s, Point p, uint8_t c) { s.SetPixel(p,c); }
'''
TEST=r'''
int main() {
    UpdateMinimapRect();
    // Including the map frame/shadow, the physical footprint is 133x80,
    // positioned in the top-right third without reaching screen edges.
    const int left=(MinimapRect.position.x-2)*400/640;
    const int right=(MinimapRect.position.x+MinimapRect.size.width+2)*400/640;
    assert(right-left>=132 && right-left<=134);
    assert(MinimapRect.size.height+4==80);
    assert(left>=266 && right<=398);
    assert(MinimapRect.position.y-2>=0);
    Surface out;
    for(int y=0;y<240;++y) for(int x=0;x<640;++x) SetMapPixel(out,{x,y},42);
    for(int y=0;y<240;++y) for(int x=0;x<640;++x)
        assert(out.pixels[y*640+x]==(MinimapRect.contains({x,y})?42:7));
    // Full and transparent modes retain the existing full-screen map extent.
    for(auto m : {AutomapType::Opaque,AutomapType::Transparent}) {
        mode=m;SetMapPixel(out,{20,120},51);assert(out.pixels[120*640+20]==51);
    }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source=Path(tmp)/'test.cpp';exe=Path(tmp)/'test'
    source.write_text(CPP+extract('Source/automap.cpp','void UpdateMinimapRect()')+'\n'+extract('Source/engine/render/automap_render.cpp','void SetMapPixel(')+TEST)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-D__3DS__','-I'+str(ROOT/'Source'),str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: 133x80 map footprint; map pixels stay inside the panel')
