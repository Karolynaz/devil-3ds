#!/usr/bin/env python3
"""Run production compact spell paging and seven-slot binding logic on the host."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'Source/panels/spell_book.cpp').read_text()
def function(signature):
    start = source.index(signature)
    opening = source.index('\n{', start) + 1
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
enums = (ROOT / 'Source/tables/spelldat.h').read_text()
def enum(name):
    start = enums.index('enum class ' + name)
    return enums[start:enums.index('};', start) + 2]
table = source[source.index('const SpellID SpellPages['):source.index('\n#ifdef __3DS__', source.index('const SpellID SpellPages['))]
prelude = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <vector>
#include "controls/controller_buttons.h"
using namespace devilution;
'''+enum('SpellID')+enum('SpellType')+r'''
constexpr size_t SpellBookPages=6, SpellBookPageEntries=7;
constexpr int CtrSpellRowsPerPage=5;
struct Player {
    int _pClass=0;
    uint64_t _pAblSpells=0,_pMemSpells=0,_pISpells=0;
    SpellID _pRSpell=SpellID::Invalid;
    SpellType _pRSplType=SpellType::Invalid;
    SpellID _pSplHotKey[12];
    SpellType _pSplTHotKey[12];
} player;
Player *InspectPlayer=&player, *MyPlayer=&player;
struct Loadout { SpellID skill; };
Loadout GetPlayerStartingLoadoutForClass(int clazz) {
    return {clazz==0?SpellID::ItemRepair:SpellID::StaffRecharge};
}
bool IsValidSpell(SpellID spell) { return spell>SpellID::Null && spell<=SpellID::LAST; }
uint64_t GetSpellBitmask(SpellID spell) { assert(IsValidSpell(spell)); return uint64_t(1)<<(int(spell)-1); }
int SpellbookTab=0;
bool SpellbookFlag=true, inspecting=false, bindingOpen=false;
SpellID bindingSpell=SpellID::Invalid, focused=SpellID::Invalid;
SpellType bindingType=SpellType::Invalid;
bool IsInspectingPlayer() { return inspecting; }
SpellID FocusedCtrSpell() { return focused; }
void RedrawEverything() {}
'''
functions = '\n'.join(function(s) for s in [
    'const std::vector<SpellID> &KnownCtrSpells()',
    'SpellID GetSpellFromSpellPage(', 'int CtrSpellbookPageCount()',
    'int CtrSpellbookRowCount()', 'SpellType CtrSpellSource(',
    'int CtrSpellBindingSlot(', 'bool HandleCtrSpellBinding('])
test = r'''
int main() {
    for(auto &spell:player._pSplHotKey) spell=SpellID::Invalid;
    // Sparse original pages must collapse without gaps or duplicate entries.
    player._pAblSpells=GetSpellBitmask(SpellID::ItemRepair);
    player._pMemSpells=GetSpellBitmask(SpellID::Search)|GetSpellBitmask(SpellID::Fireball);
    assert(CtrSpellbookPageCount()==1);
    assert(KnownCtrSpells().size()==3);
    assert(GetSpellFromSpellPage(0,0)==SpellID::ItemRepair);
    assert(GetSpellFromSpellPage(0,1)==SpellID::Fireball);
    assert(GetSpellFromSpellPage(0,2)==SpellID::Search);
    assert(GetSpellFromSpellPage(0,3)==SpellID::Invalid);
    assert(GetSpellFromSpellPage(std::numeric_limits<size_t>::max(),0)==SpellID::Invalid);
    // Every possible ownership count, including page boundaries and empty heroes.
    for(int n=0;n<=int(SpellID::LAST);++n) {
        player._pAblSpells=0;
        player._pMemSpells=n==0?0:(uint64_t(1)<<n)-1;
        const auto &known=KnownCtrSpells();
        assert(int(known.size())==n);
        assert(CtrSpellbookPageCount()==std::max(1,(n+4)/5));
        uint64_t seen=0;
        for(SpellbookTab=0;SpellbookTab<CtrSpellbookPageCount();++SpellbookTab) {
            assert(CtrSpellbookRowCount()==std::clamp(n-SpellbookTab*5,0,5));
            for(int row=0;row<5;++row) {
                auto spell=GetSpellFromSpellPage(SpellbookTab,row);
                if(SpellbookTab*5+row<n) {
                    assert(IsValidSpell(spell));
                    auto bit=GetSpellBitmask(spell);assert(!(seen&bit));seen|=bit;
                } else assert(spell==SpellID::Invalid);
            }
        }
        assert(seen==player._pMemSpells);
    }
    // Bind all seven gestures. Modifier alone waits; B cancels; L+B assigns.
    const ControllerButton buttons[]={ControllerButton_BUTTON_X,ControllerButton_BUTTON_RIGHTSHOULDER,
        ControllerButton_BUTTON_RIGHTSHOULDER,ControllerButton_BUTTON_A,ControllerButton_BUTTON_B,
        ControllerButton_BUTTON_X,ControllerButton_BUTTON_Y};
    focused=SpellID::Fireball;
    for(int slot=0;slot<7;++slot) {
        bindingOpen=false;
        assert(HandleCtrSpellBinding(ControllerButton_BUTTON_Y,false)&&bindingOpen);
        assert(HandleCtrSpellBinding(ControllerButton_BUTTON_LEFTSHOULDER,true)&&bindingOpen);
        assert(CtrSpellBindingSlot(buttons[slot],slot>=2)==slot);
        assert(HandleCtrSpellBinding(buttons[slot],slot>=2)&&!bindingOpen);
        assert(player._pSplHotKey[slot]==focused);
        assert(player._pSplTHotKey[slot]==SpellType::Spell);
    }
    assert(player._pRSpell==SpellID::Fireball);
    HandleCtrSpellBinding(ControllerButton_BUTTON_Y,false);
    HandleCtrSpellBinding(ControllerButton_BUTTON_B,false);assert(!bindingOpen);
    player._pISpells=GetSpellBitmask(focused);
    HandleCtrSpellBinding(ControllerButton_BUTTON_Y,false);
    HandleCtrSpellBinding(ControllerButton_BUTTON_RIGHTSHOULDER,false);
    assert(player._pSplTHotKey[1]==SpellType::Charges);
    player._pAblSpells=GetSpellBitmask(focused);
    HandleCtrSpellBinding(ControllerButton_BUTTON_Y,false);
    HandleCtrSpellBinding(ControllerButton_BUTTON_RIGHTSHOULDER,false);
    assert(player._pSplTHotKey[1]==SpellType::Skill);
    inspecting=true;
    assert(!HandleCtrSpellBinding(ControllerButton_BUTTON_Y,false));
    SpellbookFlag=false;bindingOpen=true;
    assert(!HandleCtrSpellBinding(ControllerButton_BUTTON_Y,false)&&!bindingOpen);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'test.cpp'; exe=Path(tmp)/'test'
    cpp.write_text(prelude+table+functions+test)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-D__3DS__','-fsanitize=address,undefined','-I'+str(ROOT/'Source'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: compact spell pages, all ownership counts, seven bindings and cancel (ASan/UBSan)')
