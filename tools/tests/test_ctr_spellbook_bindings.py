#!/usr/bin/env python3
"""Run production compact spell paging and four-slot binding logic on the host."""
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
#include <string>
#include <cstdio>
#include <cstdlib>
#include "controls/controller_buttons.h"
#include "controls/axis_direction.h"
#include "platform/ctr/ui_geometry.hpp"
using namespace devilution;
// Project headers can disable assert in release mode; checks must still execute.
#undef assert
#define assert(condition) do { if (!(condition)) { std::fprintf(stderr,"Check failed at %d: %s\n",__LINE__,#condition); std::abort(); } } while(false)
'''+enum('SpellID')+enum('SpellType')+r'''
constexpr size_t SpellBookPages=6, SpellBookPageEntries=7;
Point MousePosition{};
Point CtrScreenToTop(Point p) { return p; }
Point CtrTopToScreen(Point p) { return p; }
void SetCursorPos(Point p) { MousePosition=p; }
constexpr size_t NumHotkeys=12;
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
    'int CtrSpellbookRowCount()', 'SpellType CtrSpellSource(', 'void MoveCtrSpellBinding(',
    'int CtrSpellBindingSlot(', 'std::string CtrSpellShortcutText(', 'bool HandleCtrSpellBinding('])
save_source = (ROOT / 'Source/loadsave.cpp').read_text()
start = save_source.index('void MigrateCtrSpellShortcuts(')
opening = save_source.index('\n{', start) + 1
end, depth = opening + 1, 1
while depth:
    depth += (save_source[end] == '{') - (save_source[end] == '}')
    end += 1
functions += '\n' + save_source[start:end]
test = r'''
int main() {
    player._pSplHotKey[1]=SpellID::Firebolt;
    player._pSplTHotKey[1]=SpellType::Spell;
    player._pSplHotKey[2]=SpellID::Healing;
    player._pSplTHotKey[2]=SpellType::Skill;
    MigrateCtrSpellShortcuts(player);
    assert(player._pSplHotKey[3]==SpellID::Firebolt);
    assert(player._pSplTHotKey[3]==SpellType::Spell);
    assert(player._pSplHotKey[1]==SpellID::Invalid && player._pSplHotKey[2]==SpellID::Invalid);
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
        assert(CtrSpellbookPageCount()==std::max(1,(n+CtrSpellRowsPerPage-1)/CtrSpellRowsPerPage));
        uint64_t seen=0;
        for(SpellbookTab=0;SpellbookTab<CtrSpellbookPageCount();++SpellbookTab) {
            assert(CtrSpellbookRowCount()==std::clamp(n-SpellbookTab*CtrSpellRowsPerPage,0,CtrSpellRowsPerPage));
            for(int row=0;row<CtrSpellRowsPerPage;++row) {
                auto spell=GetSpellFromSpellPage(SpellbookTab,row);
                if(SpellbookTab*CtrSpellRowsPerPage+row<n) {
                    assert(IsValidSpell(spell));
                    auto bit=GetSpellBitmask(spell);assert(!(seen&bit));seen|=bit;
                } else assert(spell==SpellID::Invalid);
            }
        }
        assert(seen==player._pMemSpells);
    }
    // Every icon has its own hit target; borders and empty space cannot bind.
    for(int slot=0;slot<CtrSpellShortcutCount;++slot) {
        assert(CtrSpellBindingHit(CtrSpellBindingRect(slot).Center())==slot);
        assert(CtrSpellBindingHit(CtrSpellBindingPositions[slot])==slot);
    }
    assert(CtrSpellBindingHit({0,0})==-1);
    // Cursor navigation visits every slot and A binds the focused target.
    const int route[]={0,1,2,3};
    focused=SpellID::Fireball;
    for(int slot:route) {
        HandleCtrSpellBinding(ControllerButton_BUTTON_Y,false);
        MousePosition=CtrSpellBindingRect(slot).Center();
        assert(HandleCtrSpellBinding(ControllerButton_BUTTON_A,false)&&!bindingOpen);
        assert(player._pSplHotKey[slot]==focused);
    }
    MousePosition=CtrSpellBindingRect(0).Center();
    MoveCtrSpellBinding({AxisDirectionX_RIGHT,AxisDirectionY_NONE});assert(CtrSpellBindingHit(MousePosition)==1);
    MoveCtrSpellBinding({AxisDirectionX_RIGHT,AxisDirectionY_NONE});assert(CtrSpellBindingHit(MousePosition)==2);
    MoveCtrSpellBinding({AxisDirectionX_RIGHT,AxisDirectionY_NONE});assert(CtrSpellBindingHit(MousePosition)==3);
    MoveCtrSpellBinding({AxisDirectionX_RIGHT,AxisDirectionY_NONE});assert(CtrSpellBindingHit(MousePosition)==3);
    for(int slot:{2,1,0}) {
        MoveCtrSpellBinding({AxisDirectionX_LEFT,AxisDirectionY_NONE});assert(CtrSpellBindingHit(MousePosition)==slot);
    }
    HandleCtrSpellBinding(ControllerButton_BUTTON_Y,false);
    MousePosition={0,0};
    HandleCtrSpellBinding(ControllerButton_BUTTON_A,false);assert(bindingOpen);
    HandleCtrSpellBinding(ControllerButton_BUTTON_B,false);assert(!bindingOpen);
    // Four independent shortcuts; holding L never changes another binding.
    const ControllerButton buttons[]={ControllerButton_BUTTON_X,ControllerButton_BUTTON_Y,
        ControllerButton_BUTTON_LEFTSHOULDER,ControllerButton_BUTTON_RIGHTSHOULDER};
    focused=SpellID::Fireball;
    for(int slot=0;slot<CtrSpellShortcutCount;++slot) {
        bindingOpen=false;
        assert(HandleCtrSpellBinding(ControllerButton_BUTTON_Y,false)&&bindingOpen);
        assert(CtrSpellBindingSlot(buttons[slot],false)==slot);
        assert(CtrSpellBindingSlot(buttons[slot],true)==slot);
        assert(HandleCtrSpellBinding(buttons[slot],true)&&!bindingOpen);
        assert(player._pSplHotKey[slot]==focused);
        assert(player._pSplTHotKey[slot]==SpellType::Spell);
    }
    assert(player._pRSpell==SpellID::Fireball);
    assert(CtrSpellShortcutText(focused,SpellType::Spell)=="(X) (Y) (L) (R)");
    assert(CtrSpellShortcutText(SpellID::Healing,SpellType::Spell).empty());
    assert(CtrSpellShortcutText(focused,SpellType::Charges).empty());
    player._pRSpell=SpellID::Healing;
    assert(CtrSpellShortcutText(SpellID::Healing,SpellType::Spell)=="(X)");
    assert(CtrSpellShortcutText(focused,SpellType::Spell)=="(Y) (L) (R)");
    player._pRSpell=focused;
    HandleCtrSpellBinding(ControllerButton_BUTTON_Y,false);
    HandleCtrSpellBinding(ControllerButton_BUTTON_B,false);assert(!bindingOpen);
    player._pISpells=GetSpellBitmask(focused);
    HandleCtrSpellBinding(ControllerButton_BUTTON_Y,false);
    HandleCtrSpellBinding(ControllerButton_BUTTON_RIGHTSHOULDER,false);
    assert(player._pSplTHotKey[3]==SpellType::Charges);
    player._pAblSpells=GetSpellBitmask(focused);
    HandleCtrSpellBinding(ControllerButton_BUTTON_Y,false);
    HandleCtrSpellBinding(ControllerButton_BUTTON_RIGHTSHOULDER,false);
    assert(player._pSplTHotKey[3]==SpellType::Skill);
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
print('PASS: compact spell pages, all ownership counts, four bindings and cancel (ASan/UBSan)')
