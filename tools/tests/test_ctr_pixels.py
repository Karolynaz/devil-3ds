#!/usr/bin/env python3
"""Check native screen sampling, fixed camera, and whole-sprite stereo phase."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
TEST = r'''
#include <array>
#include <cstdlib>
#include <initializer_list>
#include "platform/ctr/pixel_geometry.hpp"
#include "platform/ctr/world_view.hpp"
using namespace devilution;
void check(bool ok) { if (!ok) std::abort(); }
int main() {
    // No camera size depends on the hardware slider.
    check(CtrWorldWidth() == 640 && CtrWorldHeight() == 240);
    for (int width : {320, 400}) {
        std::array<int, 640> logical{};
        for (int x = 0; x < 640; ++x) logical[x] = CtrInverseColumn(x, width);
        for (int x = 0; x < width; ++x) {
            check(logical[CtrNativeColumn(x, width)] == x);
            check(CtrInverseColumn(CtrNativeColumn(x, width), width) == x);
        }
        // A native image keeps its exact columns at every screen position.
        for (int origin = 0; origin < width; ++origin)
            for (int x = origin; x < width; ++x)
                check(logical[CtrNativeColumn(x, width)] - origin == x - origin);
    }
    check(CtrFitImage({320, 200}, {400, 240}) == Size(320, 200));
    check(CtrFitImage({640, 480}, {400, 240}) == Size(320, 240));
    check(CtrFitImage({180, 76}, {90, 76}) == Size(90, 38));
    check(CtrFitImage({28, 28}, {16, 16}) == Size(16, 16));
    check(CtrFitImage({0, 28}, {16, 16}) == Size(0, 0));
    check(CtrStereoOffsetFor(0) == 0);
    check(CtrStereoOffsetFor(0.3f) == -8);
    check(CtrStereoOffsetFor(1) == -16);
    // Every row of an actor samples the exact same sprite columns in both eyes.
    for (int disparity : {5, 10})
        for (int x = 0; x + disparity < 400; ++x)
            check(CtrNativeColumn(x + disparity, 400) - CtrNativeColumn(x, 400) == disparity * 8 / 5);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'pixels.cpp'
    exe = Path(tmp) / 'pixels'
    source.write_text(TEST)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT/'Source'), str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
# Full supplied inventory frames, not just a synthetic line: verify every byte
# through the same existing expand/present convention (400 -> 640 -> 400).
for asset in sorted((ROOT/'assets/data').glob('ctr_inventory_*.pal8')):
    image = asset.read_bytes()
    assert len(image) == 400*240
    assert 1 not in image and 2 not in image, f'{asset.name}: reserved UI key'
    expanded = bytes(image[y*400 + ((x+1)*400+639)//640-1]
                     for y in range(240) for x in range(640))
    presented = bytes(expanded[y*640+x*640//400]
                      for y in range(240) for x in range(400))
    assert presented == image, f'{asset.name}: native pixels changed'
print('PASS: all six inventory frames unchanged, native UI grid, aspect fit, fixed camera, stereo phase')
