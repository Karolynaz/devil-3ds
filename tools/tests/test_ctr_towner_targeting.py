#!/usr/bin/env python3
"""Check 3DS controller focus among nearby towners without launching the game."""

from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    opening = source.index("\n{", start) + 1
    end = opening + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


CPP = r'''
#include <array>
#include <cstddef>
#include "engine/point.hpp"
using namespace devilution;
struct Towner { Point position; int _ttype; };
std::array<Towner, 4> Towners{};
size_t townerCount = 0;
int pcursmonst = -1;
size_t GetNumTowners() { return townerCount; }
int GetDistance(Point position, int) { return position.x; }
int GetRotaryDistance(Point position) { return position.y; }
bool IsTownerPresent(int) { return true; }
void check(bool ok) { if (!ok) std::abort(); }
'''

TEST = r'''
int main() {
    // The closest reachable NPC is behind the player; focus the forward NPC.
    Towners[0] = {{1, 2}, 0};
    Towners[1] = {{2, 1}, 0};
    Towners[2] = {{2, 0}, 0};
    townerCount = 3;
    CheckTownersNearby();
    check(pcursmonst == 2);

    // Equal distance prefers the NPC requiring the least turn, independent of list order.
    pcursmonst = -1;
    Towners[0] = {{1, 1}, 0};
    Towners[1] = {{1, 0}, 0};
    townerCount = 2;
    CheckTownersNearby();
    check(pcursmonst == 1);

    // Equal distance and direction keeps the first target deterministically.
    pcursmonst = -1;
    Towners[0] = {{1, 0}, 0};
    Towners[1] = {{1, 0}, 0};
    townerCount = 2;
    CheckTownersNearby();
    check(pcursmonst == 0);
}
'''

with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / "towner.cpp"
    executable = Path(tmp) / "towner"
    source.write_text(CPP + function("Source/controls/plrctrls.cpp", "void CheckTownersNearby(") + TEST)
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-D__3DS__", "-Wall", "-Wextra", "-Werror",
        "-I" + str(ROOT / "Source"), str(source), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)

print("PASS: 3DS towner focus chooses a forward, reachable target by distance and turn")
