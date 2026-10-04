#!/usr/bin/env python3
"""Check production spellbook lookup bounds, the Hellfire page and class skills."""
import csv
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'Source/panels/spell_book.cpp').read_text()
enum = (ROOT / 'Source/tables/spelldat.h').read_text().split('enum class SpellID : int8_t {', 1)[1].split('};', 1)[0]
pages = source[source.index('const size_t SpellBookPages'):source.index('constexpr uint16_t SpellBookButtonWidthDiablo')]
table = source[source.index('const SpellID SpellPages['):source.index('\nSpellID GetSpellFromSpellPage(')]
start = source.index('SpellID GetSpellFromSpellPage(')
opening = source.index('\n{', start) + 1
end, depth = opening + 1, 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
lookup = source[start:end]

# Starting skills come from the shipped class data, rather than a copied mapping.
classes = list(csv.DictReader((ROOT / 'mods/hf/txtdata/classes/classdat.tsv').read_text().splitlines(), delimiter='\t'))
assert len(classes) == 6
loadouts = {}
for row in classes:
    clazz = row['folderName']
    path = ROOT / f'mods/hf/txtdata/classes/{clazz}/starting_loadout.tsv'
    if not path.exists():
        path = ROOT / f'assets/txtdata/classes/{clazz}/starting_loadout.tsv'
    data = path.read_text()
    skill = re.search(r'^skill\t([^\r\n]+)', data, re.M)
    assert skill, path
    loadouts[clazz] = skill[1]

prelude = '''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
enum class SpellID : int8_t {''' + enum + '''};
enum class HeroClass { Warrior, Rogue, Sorcerer, Monk, Bard, Barbarian };
struct Player { HeroClass _pClass; } player;
Player *InspectPlayer = &player;
struct Loadout { SpellID skill; };
Loadout GetPlayerStartingLoadoutForClass(HeroClass clazz) {
    switch (clazz) {
'''
for clazz, skill in loadouts.items():
    prelude += f'case HeroClass::{clazz.title()}: return {{SpellID::{skill}}};\n'
prelude += '    } return {SpellID::Invalid};\n}\n'
test = '''
int main() {
    const SpellID hellfire[] = { SpellID::LightningWall, SpellID::Immolation,
        SpellID::Warp, SpellID::Reflect, SpellID::Berserk, SpellID::RingOfFire, SpellID::Search };
    for (size_t i=0; i<7; ++i) assert(GetSpellFromSpellPage(4, i)==hellfire[i]);
    for (size_t page : {size_t(6), size_t(7), std::numeric_limits<size_t>::max()})
        assert(GetSpellFromSpellPage(page, 0)==SpellID::Invalid);
    for (size_t entry : {size_t(7), size_t(8), std::numeric_limits<size_t>::max()})
        assert(GetSpellFromSpellPage(0, entry)==SpellID::Invalid);
    for (size_t i=0; i<7; ++i) assert(GetSpellFromSpellPage(5, i)==SpellID::Invalid);
'''
for clazz, skill in loadouts.items():
    test += f'player._pClass=HeroClass::{clazz.title()}; assert(GetSpellFromSpellPage(0,0)==SpellID::{skill});\n'
test += '}\n'
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'spellbook.cpp'
    exe = Path(directory) / 'spellbook'
    cpp.write_text('#include <initializer_list>\n' + prelude + pages + table + lookup + test)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-fsanitize=address,undefined', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: Hellfire spellbook page, all six class skills and invalid lookup bounds (ASan/UBSan)')
