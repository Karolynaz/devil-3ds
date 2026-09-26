#!/usr/bin/env python3
"""Compile and exercise the 3DS coordinate transforms against actual inventory slots.
Run: python3 tools/tests/test_ctr_ui_geometry.py (requires a C++17 compiler).
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'Source/inv.cpp').read_text()
table = source.split('const Rectangle InvRect[] = {', 1)[1].split('\n};', 1)[0]
slots = re.findall(r'\{ \{\s*(\d+),\s*(\d+) \}, \{\s*(\d+),\s*(\d+) \} \}', table)
assert len(slots) == 55, 'Inventory layout changed: review this geometry test'
rectangles = ',\n'.join('{{' + x + ',' + y + '},{' + w + ',' + h + '}}' for x,y,w,h in slots)
test = r'''
#include <cstdlib>
#include <cmath>
#include "platform/ctr/ui_geometry.hpp"
using namespace devilution;
void check(bool ok) { if (!ok) std::abort(); }
int main() {
 const Rectangle slots[] = { SLOTS };
 for (bool split : {false, true}) {
  for (int i = 0; i < 47; ++i) {
   Point screen = CtrInventoryToScreen(slots[i].Center(), 640, split);
   check(slots[i].contains(CtrScreenToInventory(screen, 640, split)));
  }
  // Every bottom-screen coordinate must be excluded from inventory hit testing,
  // including the gap above the belt and all eight actual belt slot centers.
  for (int x = 0; x < 640; ++x)
   for (int y = 240; y < 480; ++y)
    check(CtrScreenToInventory({x,y},640,split) == Point(-1,-1));
  for (int i = 47; i < 55; ++i) {
   Point belt = slots[i].Center() + Displacement {0,352};
   check(CtrScreenToInventory(belt,640,split) == Point(-1,-1));
   check(slots[i].contains(belt - Displacement {0,352}));
  }
  const Rectangle r = CtrInventoryScreenRect(640,split);
  check(CtrScreenToInventory({r.position.x-1,100},640,split) == Point(-1,-1));
  check(CtrScreenToInventory({r.position.x+r.size.width,100},640,split) == Point(-1,-1));
  // Center of every possible multi-cell item remains inside its intended footprint.
  for (int w=1; w<=2; ++w) for(int h=1;h<=3;++h)
   for(int row=0;row<=4-h;++row) for(int col=0;col<=10-w;++col) {
    const Rectangle first=slots[7+row*10+col];
    Point p=CtrInventoryToScreen(first.Center(),640,split);
    p += Displacement {(w-1)*29*r.size.width/(2*320),(h-1)*29*r.size.height/(2*352)};
    const Point local=CtrScreenToInventory(p,640,split);
    check((Rectangle {first.position,{w*29,h*29}}).contains(local));
   }
 }
 for(const Rectangle b : CtrStatButtons) {
  check(b.contains(CtrScreenToTop(CtrTopToScreen(b.Center(),640),640)));
  check(b.position.y+b.size.height<=240);
 }
 for(int row=0;row<7;++row) {
  const Point p {20,CtrSpellRowsY+row*CtrSpellRowHeight+12};
  const Point screen=CtrTopToScreen(p,640);
  check((CtrScreenToTop(screen,640).y-CtrSpellRowsY)/CtrSpellRowHeight==row);
 }
 check(CtrSpellRowsY+7*CtrSpellRowHeight<CtrSpellTabsY);
 check(CtrBottomHealthBar.position==Point(20,82) && CtrBottomHealthBar.size==Size(26,84));
 check(CtrBottomManaBar.position==Point(274,82) && CtrBottomManaBar.size==Size(26,84));
 check(CtrBottomExperienceBar.position==Point(48,206) && CtrBottomExperienceBar.size==Size(224,14));
 check(CtrBottomInfoBox.position==Point(64,75) && CtrBottomInfoBox.size==Size(191,118));
 for(int slot=0;slot<8;++slot) {
  const Rectangle belt=CtrBottomBeltSlot(slot);
  check(belt.position.x==69+slot*23+(slot>=4?1:0));
  check(belt.position.y==36 && belt.size==Size(21,21));
  if(slot) check(CtrBottomBeltSlot(slot-1).position.x+21<=belt.position.x);
 }
 for(int page=0;page<4;++page) {
  const Rectangle tab=CtrSpellTabRect(page,false);
  check(tab.position.x==16+95*page);
  check(tab.position.y==240-9-17);
  check(tab.size==Size(85,17));
  check(tab.position.x+tab.size.width<=400-9);
 }
 const Rectangle inv=CtrInventoryScreenRect(640,false);
 check(inv.position.x*2+inv.size.width==640);
 // Physical pixels preserve the 320:352 original item/slot aspect ratio.
 check(std::abs(inv.size.width*400.0/640.0/240.0-320.0/352.0)<0.003);
}
'''.replace('SLOTS',rectangles)
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'ctr_ui_test.cpp'
    exe=Path(tmp)/'ctr_ui_test'
    cpp.write_text(test)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-I'+str(ROOT/'Source'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: equipment, 40 inventory slots, 8 belt slots, multi-cell items, stat buttons, spell tabs, screen boundaries, aspect ratio')
