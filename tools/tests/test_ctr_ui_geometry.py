#!/usr/bin/env python3
"""Check native 3DS UI coordinates without launching the game.

Run: python3 tools/tests/test_ctr_ui_geometry.py
"""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = r'''
#include <cstdlib>
#include <initializer_list>
#include "platform/ctr/ui_geometry.hpp"
using namespace devilution;
void check(bool ok) { if (!ok) std::abort(); }
int main() {
  for (bool split : {false, true}) {
    const int shift = split ? 97 : 0;
    check(CtrInventorySlotRect(0, split).position == Point(183 + shift, 20));
    check(CtrInventorySlotRect(3, split).position == Point(234 + shift, 37));
    check(CtrInventorySlotRect(4, split).position == Point(116 + shift, 64));
    check(CtrInventorySlotRect(5, split).position == Point(250 + shift, 64));
    check(CtrInventorySlotRect(6, split).position == Point(183 + shift, 64));
    for (int i = 7; i <= 46; ++i) {
      const Rectangle cell = CtrInventorySlotRect(i, split);
      check(cell.position == Point(115 + shift + (i - 7) % 10 * 17,
                                   152 + (i - 7) / 10 * 17));
      check(cell.size == Size(16, 16));
      check(cell.position.x >= 9 && cell.position.x + cell.size.width <= 391);
      check(cell.position.y >= 12 && cell.position.y + cell.size.height <= 227);
    }
  }
  for (int y = 0; y < 10; ++y) for (int x = 0; x < 10; ++x) {
    const Rectangle cell = CtrStashSlotRect({x, y});
    check(cell.position == Point(19 + x * 17, 50 + y * 17));
    check(cell.size == Size(16, 16));
    check(cell.position.x + cell.size.width <= 190);
    check(cell.position.y + cell.size.height <= 219);
  }
  for (int x = 0; x < 640; ++x) {
    check(CtrScreenToTop({x, 240}, 640) == Point(-1, -1));
    const Point top = CtrScreenToTop({x, 120}, 640);
    check(top.x >= 0 && top.x < 400 && top.y == 120);
  }
  for(bool hellfire : {false,true}) {
    Rectangle previous {};
    for(int page=0; page<(hellfire?5:4); ++page) {
      const Rectangle tab=CtrSpellTabRect(page,hellfire);
      check(tab.position.x >= 10 && tab.position.x+tab.size.width <= 390);
      check(tab.position.y >= 214 && tab.position.y+tab.size.height <= 234);
      if(page) check(previous.position.x+previous.size.width < tab.position.x);
      previous=tab;
    }
  }
  check(CtrBottomHealthBar.position == Point(20, 82));
  check(CtrBottomManaBar.position == Point(274, 82));
  check(CtrBottomExperienceBar.size == Size(224, 14));
}
'''

with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'ctr_ui_test.cpp'
    executable = Path(tmp) / 'ctr_ui_test'
    source.write_text(TEST)
    subprocess.run([
        os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
        '-I' + str(ROOT / 'Source'), str(source), '-o', str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print('PASS: inventory, stash, Diablo/Hellfire spell tabs and bottom-screen UI geometry')
