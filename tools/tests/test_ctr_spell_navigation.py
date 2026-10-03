#!/usr/bin/env python3
"""Execute production 3DS spell layout, paging and controller movement on host."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'Source/panels/spell_list.cpp').read_text()
def function(signature):
    start=source.index(signature); opening=source.index('\n{',start)+1; end=opening+1; depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
PREFIX=r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>
#include "controls/axis_direction.h"
#include "platform/ctr/spell_geometry.hpp"
#include "platform/ctr/pixel_geometry.hpp"
using namespace devilution;
enum class SpellType {Skill,Spell,Scroll,Charges,Invalid};
enum class SpellID:int8_t {Invalid=-1,Null=0,Firebolt=1};
template <typename T> auto enum_values() { return std::array<SpellType,5>{SpellType::Skill,SpellType::Spell,SpellType::Scroll,SpellType::Charges,SpellType::Invalid}; }
struct SpellListItem { Point location;SpellType type;SpellID id;bool isSelected;bool isVisible=true; };
struct Player {uint64_t _pAblSpells=0,_pMemSpells=0,_pScrlSpells=0,_pISpells=0;SpellID _pRSpell=SpellID::Invalid;SpellType _pRSplType=SpellType::Invalid;};
Player player;Player *MyPlayer=&player;
std::array<int,54> SpellsData{};
Point MousePosition{};
bool SpellSelectFlag=false;
void SetCursorPos(Point p) { MousePosition=p; }
constexpr std::array<SpellType,4> SpellRowTypes {SpellType::Spell,SpellType::Charges,SpellType::Skill,SpellType::Scroll};
std::array<size_t,4> SpellRowFirst{};
std::vector<SpellListItem> GetSpellListItems();
bool Focus3dsSpellListItem(SpellID,SpellType);
void check(bool ok) { if(!ok) std::abort(); }
'''
TEST=r'''
SpellListItem selected() {
    for(const auto &item:GetSpellListItems()) if(item.isSelected) return item;
    std::abort();
}
void checkBounds() {
    std::array<int,4> visible{};
    for(const auto &item:GetSpellListItems()) {
        if(!item.isVisible) { check(!item.isSelected);continue; }
        ++visible[SpellRowIndex(item.type)];
        const int x=CtrInverseColumn(item.location.x,400);
        check(x>=0 && x+56<=400 && item.location.y-56>=0 && item.location.y<=240);
    }
    for(auto count:visible) check(count<=6);
}
int main() {
    // All classes use the same masks; their innate ability differs.
    for(int skill:{26,27,28}) {
        player={};player._pAblSpells=uint64_t(1)<<(skill-1);
        player._pMemSpells=(uint64_t(1)<<20)-1;
        player._pScrlSpells=(uint64_t(1)<<15)-1;
        player._pISpells=uint64_t(1)<<2;
        player._pRSpell=SpellID(19);player._pRSplType=SpellType::Spell;
        DoSpeedBook();check(SpellSelectFlag);checkBounds();
        check(selected().id==SpellID(19));
        for(int id=18;id>=1;--id) {
            Move3dsSpellListSelection({AxisDirectionX_LEFT,AxisDirectionY_NONE});
            check(selected().type==SpellType::Spell && selected().id==SpellID(id));checkBounds();
        }
        Move3dsSpellListSelection({AxisDirectionX_LEFT,AxisDirectionY_NONE});check(selected().id==SpellID(1));
        for(int id=2;id<=20;++id) {
            Move3dsSpellListSelection({AxisDirectionX_RIGHT,AxisDirectionY_NONE});
            check(selected().type==SpellType::Spell && selected().id==SpellID(id));checkBounds();
        }
        Move3dsSpellListSelection({AxisDirectionX_RIGHT,AxisDirectionY_NONE});check(selected().id==SpellID(20));
        for(auto type:{SpellType::Charges,SpellType::Skill,SpellType::Scroll}) {
            Move3dsSpellListSelection({AxisDirectionX_NONE,AxisDirectionY_DOWN});check(selected().type==type);checkBounds();
        }
        check(Focus3dsSpellListItem(SpellID(1),SpellType::Scroll));
        for(int id=2;id<=15;++id) {
            Move3dsSpellListSelection({AxisDirectionX_RIGHT,AxisDirectionY_NONE});
            check(selected().type==SpellType::Scroll && selected().id==SpellID(id));checkBounds();
        }
        // The same spell in the spell and scroll rows keeps distinct types.
        check(Focus3dsSpellListItem(SpellID(3),SpellType::Spell));check(selected().type==SpellType::Spell);
        check(Focus3dsSpellListItem(SpellID(3),SpellType::Charges));check(selected().type==SpellType::Charges);
        player._pISpells=0;
        check(Focus3dsSpellListItem(SpellID(1),SpellType::Spell));
        Move3dsSpellListSelection({AxisDirectionX_NONE,AxisDirectionY_DOWN});check(selected().type==SpellType::Skill);checkBounds();
        // Reopening on an invalid active spell selects the first available entry.
        player._pRSpell=SpellID::Invalid;DoSpeedBook();check(selected().type==SpellType::Spell && selected().id==SpellID(1));
    }
    player={};DoSpeedBook();Move3dsSpellListSelection({AxisDirectionX_RIGHT,AxisDirectionY_DOWN});check(GetSpellListItems().empty());
    for(int rows=1;rows<=4;++rows) for(int r=0;r<rows;++r) for(int c=0;c<6;++c) {
        auto rect=CtrSpellRect(c,r,rows);check(rect.size==Size(56,56));
        check(rect.position.x>=0 && rect.position.x+56<=400 && rect.position.y>=0 && rect.position.y+56<=240);
    }
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory);cpp=p/'navigation.cpp';exe=p/'navigation'
    cpp.write_text(PREFIX+'\n'.join(function(s) for s in ['size_t SpellRowIndex(', 'Point SpellItemCenter(', 'std::vector<SpellListItem> GetSpellListItems(', 'bool Focus3dsSpellListItem(', 'void Move3dsSpellListSelection(', 'void DoSpeedBook('])+TEST)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-D__3DS__','-Wall','-Wextra','-Werror','-I'+str(ROOT/'Source'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: native spell rows, all entries reachable, paging, source types, all class skills, empty rows')
