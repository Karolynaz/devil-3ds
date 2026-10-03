#!/usr/bin/env python3
"""Check production camera positioning and inverse pointer mapping, without emulator."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
def function(path,signature):
    source=(ROOT/path).read_text();start=source.index(signature);opening=source.index('\n{',start)+1
    end=opening+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
PREFIX=r'''
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include "engine/rectangle.hpp"
#include "platform/ctr/world_view.hpp"
using namespace devilution;
constexpr int TILE_WIDTH=64,TILE_HEIGHT=32,MAXDUNX=112,MAXDUNY=112;
bool zoom=false;
struct Options {struct {bool *zoom;} Graphics{&zoom};} options;
Options &GetOptions() {return options;}
int GetViewportWidth() {return 640;} int GetViewportHeight() {return 240;}
int GetScreenWidth() {return 640;} int GetScreenHeight() {return 480;}
Rectangle GetMainPanel() {return {{0,240},{640,240}};}
Size SidePanelSize{320,240};
Point ViewPosition{50,50};
Displacement tileShift{},tileOffset{};int tileRows=0,tileColumns=0;
struct Player {
    int AnimInfo=0;Direction _pdir=Direction::South;
    struct Position {
        DisplacementOf<int16_t> CalculateWalkingOffsetShifted8(Direction,int) const {return {0,0};}
        DisplacementOf<int16_t> GetWalkingVelocityShifted8(Direction,int) const {return {0,0};}
    } position;
    bool isWalking() const {return false;}
} player;
Player *MyPlayer=&player;
Displacement GetOffsetForWalking(int,Direction,bool) {return {0,0};}
bool CanPanelsCoverView() {return false;} bool IsLeftPanelOpen() {return false;} bool IsRightPanelOpen() {return false;}
void check(bool ok) {if(!ok) std::abort();}
'''
FUNCTIONS=[('Source/engine/render/scrollrt.cpp',s) for s in ['void ShiftGrid(', 'int RowsCoveredByPanel(', 'void CalcTileOffset(', 'void TilesInView(', 'void CalcViewportGeometry(', 'void CalcFirstTilePosition(', 'Point GetScreenPosition(']]
FUNCTIONS += [('Source/cursor.cpp',s) for s in ['void AlterMousePositionViaZoom(', 'void AlterMousePositionViaPlayer(', 'Point ConvertToTileGrid(', 'void ShiftToDiamondGridAlignment(']]
TEST=r'''
int main() {
    for(bool z:{false,true}) {
        zoom=z;CalcViewportGeometry();
        const Point feet=GetScreenPosition(ViewPosition);
        // Bottom-left drawing convention: player anchor is 32px left, 15px below tile center.
        const int factor=zoom?2:1;
        check(feet.y== (120+CtrCameraVerticalOffset)/factor + (zoom?8:0) + 15);
        for(int dx=-3;dx<=3;++dx) for(int dy=-2;dy<=2;++dy) {
            Point tile=ViewPosition+Displacement{dx,dy};
            Point screen=GetScreenPosition(tile)+Displacement{32,-15};
            // Rendering doubles world coordinates in zoom mode; pointer conversion halves them.
            screen.x*=factor;screen.y*=factor;
            if(screen.x<0 || screen.x>=640 || screen.y<0 || screen.y>=240) continue;
            AlterMousePositionViaZoom(screen);AlterMousePositionViaPlayer(screen,player);
            Point picked=ConvertToTileGrid(screen);bool flip=false;
            ShiftToDiamondGridAlignment(screen,picked,flip);
            check(picked==tile);
        }
    }
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory); cpp=tmp/'camera.cpp';exe=tmp/'camera'
    cpp.write_text(PREFIX+'\n'.join(function(p,s) for p,s in FUNCTIONS)+TEST)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-D__3DS__','-Wall','-Wextra','-Werror',
                    '-I'+str(ROOT/'Source'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: camera moved down 28px; rendered tile centers map back to the same world tiles in both zoom modes')
