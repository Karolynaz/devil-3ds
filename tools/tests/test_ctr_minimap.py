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
int GetViewportWidth() { return 400; }
Rectangle MinimapRect;
bool leftPanelOpen=false,rightPanelOpen=false;
bool IsLeftPanelOpen() { return leftPanelOpen; }
bool IsRightPanelOpen() { return rightPanelOpen; }
struct Surface {
    mutable std::vector<uint8_t> pixels=std::vector<uint8_t>(400*240, 7);
    void SetPixel(Point p, uint8_t c) const { assert(p.x>=0 && p.x<400 && p.y>=0 && p.y<240);pixels[p.y*400+p.x]=c; }
};
void SetHalfTransparentPixel(const Surface &s, Point p, uint8_t c) { s.SetPixel(p,c); }
'''
TEST=r'''
int main() {
    // Full-map overlays shift by a quarter of the native viewport, not the
    // unchanged 640-wide UI canvas.
    leftPanelOpen=true;assert(GetAutomapPanelOffsetX()==100);
    leftPanelOpen=false;rightPanelOpen=true;assert(GetAutomapPanelOffsetX()==-100);
    leftPanelOpen=true;assert(GetAutomapPanelOffsetX()==0);
    leftPanelOpen=false;rightPanelOpen=false;
    UpdateMinimapRect();
    // Borderless dark panel is flush with the top and right screen edges.
    assert(MinimapRect.position.x==267 && MinimapRect.position.y==0);
    assert(MinimapRect.size.width==133 && MinimapRect.size.height==76);
    assert(MinimapRect.position.x+MinimapRect.size.width==400);
    Surface out;
    for(int y=0;y<240;++y) for(int x=0;x<400;++x) SetMapPixel(out,{x,y},42);
    for(int y=0;y<240;++y) for(int x=0;x<400;++x)
        assert(out.pixels[y*400+x]==(MinimapRect.contains({x,y})?42:7));
    // Full and transparent modes retain the existing full-screen map extent.
    for(auto m : {AutomapType::Opaque,AutomapType::Transparent}) {
        mode=m;SetMapPixel(out,{20,120},51);assert(out.pixels[120*400+20]==51);
    }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source=Path(tmp)/'test.cpp';exe=Path(tmp)/'test'
    source.write_text(CPP+extract('Source/automap.cpp','int GetAutomapPanelOffsetX()')+'\n'+extract('Source/automap.cpp','void UpdateMinimapRect()')+'\n'+extract('Source/engine/render/automap_render.cpp','void SetMapPixel(')+TEST)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-D__3DS__','-I'+str(ROOT/'Source'),str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: 133x76 borderless map footprint; map pixels stay inside the panel')
