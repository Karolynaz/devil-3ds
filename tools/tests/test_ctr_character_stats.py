#!/usr/bin/env python3
"""Check production 3DS character stat bindings and numeric fields without an emulator."""
from pathlib import Path
import struct, subprocess, tempfile
root=Path(__file__).resolve().parents[2];source=(root/'Source/panels/charpanel.cpp').read_text()
def function(signature):
 start=source.index(signature);opening=source.index('\n{',start)+1;end=opening+1;depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
metrics=[]
for size in (12,22):
 data=(root/f'assets/fonts/{size}-00.clx').read_bytes()
 metrics.append([struct.unpack_from('<H',data,struct.unpack_from('<I',data,4+4*i)[0]+2)[0] for i in range(128)])
prelude=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "engine/rectangle.hpp"
#include "DiabloUI/ui_flags.hpp"
#include "utils/enum_traits.h"
using namespace devilution;
enum class CharacterAttribute {Strength,Magic,Dexterity,Vitality};
enum class ItemType {Bow,Other};
enum class ItemSpecialEffect {NoMana=1};
bool HasAnyOf(ItemSpecialEffect a,ItemSpecialEffect b){return (static_cast<int>(a)&static_cast<int>(b))!=0;}
constexpr int INVLOC_HAND_LEFT=0,MaxResistance=75;
struct Player {
 std::string _pName="Hero";
 int _pBaseStr=50,_pBaseMag=40,_pBaseDex=30,_pBaseVit=20;
 int _pStrength=65,_pMagic=55,_pDexterity=45,_pVitality=35;
 int _pStatPts=4,_pGold=999999,_pExperience=2000000000;
 int _pIBonusAC=10,_pIBonusToHit=10,_pIBonusDam=10;
 int _pMaxHP=2000<<6,_pHitPoints=1000<<6,_pMaxHPBase=100<<6;
 int _pMaxMana=2000<<6,_pMana=500<<6,_pMaxManaBase=100<<6;
 int _pMagResist=75,_pFireResist=50,_pLghtResist=25;
 ItemSpecialEffect _pIFlags=static_cast<ItemSpecialEffect>(0);
 struct Item {ItemType _itype=ItemType::Other;} InvBody[1];
 std::string_view getClassName() const {return "Warrior";}
 int getCharacterLevel() const {return 40;}
 bool isMaxCharacterLevel() const {return false;}
 int getNextExperienceThreshold() const {return 2000000000;}
 int GetMaximumAttributeValue(CharacterAttribute) const {return 250;}
 int GetBaseAttributeValue(CharacterAttribute a) const {return std::array{_pBaseStr,_pBaseMag,_pBaseDex,_pBaseVit}[static_cast<int>(a)];}
 int GetCurrentAttributeValue(CharacterAttribute a) const {return std::array{_pStrength,_pMagic,_pDexterity,_pVitality}[static_cast<int>(a)];}
 int GetArmor() const {return 150;}
 int GetRangedToHit() const {return 200;}
 int GetMeleeToHit() const {return 175;}
 bool hasNoMana() const {return false;}
} player;
Player *InspectPlayer=&player;
bool inspecting=false;
bool IsInspectingPlayer(){return inspecting;}
int CalcStatDiff(const Player&){return 4;}
std::string _(const char*s){return s;}
#define N_(s) s
std::string LanguageTranslate(const std::string&s){return s;}
std::string FormatInteger(int value){return std::to_string(value);}
template<class T>std::string StrCat(T value){return std::to_string(value);}
template<class T>std::string StrCat(T value,const char*s){return std::to_string(value)+s;}
template<class T>std::string StrCat(T v,const char*s,T v2){return std::to_string(v)+s+std::to_string(v2);}
struct StyledText {UiFlags style;std::string text;};
struct Func {std::function<StyledText()> f;Func()=default;template<class F>Func(F fn):f(fn){}const auto&operator*()const{return f;}};
struct PanelEntry {std::string label;Point position;int length,labelLength;Func statDisplayFunc;};
int minDamage=250,maxDamage=350;
std::pair<int,int> GetDamage(){return {minDamage,maxDamage};}
constexpr int LeftColumnLabelX=88,TopRightLabelX=211,RightColumnLabelX=253,LeftColumnLabelWidth=76,RightColumnLabelWidth=68;
struct Surface {};
enum class CtrPanelBackground {Character};
void DrawCtrPanelBackground(const Surface&,CtrPanelBackground){}
void DrawHorizontalLine(const Surface&,Point,int,int){}
void DrawVerticalLine(const Surface&,Point,int,int){}
void FillRect(const Surface&,int,int,int,int,int){}
void UnsafeDrawBorder2px(const Surface&,Rectangle,int){}
constexpr int PAL16_BEIGE=10,PAL16_GRAY=20;
std::array<bool,4> CharPanelButton{};
std::array<Rectangle,4> CtrStatButtons{};
enum GameFontTables {GameFont12};
struct Options {UiFlags flags;int spacing=1;};
struct Draw {std::string text;Rectangle rect;UiFlags flags;};
std::vector<Draw> draws;
'''
prelude+='int metrics[2][128]={' + ','.join('{'+','.join(map(str,row))+'}' for row in metrics)+'};\n'
prelude+=r'''
int GetLineWidth(std::string_view s,GameFontTables,int spacing){int w=0;for(unsigned char c:s)w+=metrics[0][c];return w+std::max(0,int(s.size())-1)*spacing;}
void DrawString(const Surface&,std::string_view s,Rectangle r,Options o){draws.push_back({std::string(s),r,o.flags});}
void checkValues(){
 auto lookup=[](int x,int y)->const Draw& {for(const auto&d:draws)if(d.rect.position==Point{x,y})return d;std::abort();};
 for(int i=0;i<4;++i){const auto&d=lookup(132,106+i*24);assert(d.text==std::to_string(player.GetCurrentAttributeValue(static_cast<CharacterAttribute>(i))));}
 assert(lookup(65,86).text=="2000");assert(lookup(164,86).text=="2000");
 assert(lookup(291,42).text=="999999");assert(lookup(87,42).text=="40");
 assert(lookup(102,59).text=="2000000000");assert(lookup(296,59).text=="2000000000");
 assert(lookup(170,208).text=="4");
 std::array<std::string,6> expected={"230","175%",StrCat(minDamage,"-",maxDamage),"75%","50%","25%"};
 for(int i=0;i<6;++i){bool found=false;for(const auto&d:draws)if(d.rect.position.y==87+i*23 && d.rect.position.x>216){assert(d.text==expected[i]);found=true;}assert(found);}
 for(const auto&d:draws){
  if(d.text.empty()||d.text.find_first_not_of("0123456789-%")==std::string::npos){
   int font=HasAnyOf(d.flags,UiFlags::FontSizeDialog)?1:0,w=0;for(unsigned char c:d.text)w+=metrics[font][c];
   // Fit spacing can tighten by at most one pixel between each glyph.
   if(HasAnyOf(d.flags,UiFlags::KerningFitSpacing))w-=std::max(0,int(d.text.size())-1);
   assert(w<=d.rect.size.width);
  }
 }
}
int main(){
 DrawChr3DS({});checkValues();bool yellow=false;for(const auto&d:draws)if(d.rect.position==Point{132,106})yellow=HasAnyOf(d.flags,UiFlags::ColorDialogYellow);assert(yellow);
 draws.clear();player._pStrength=35;player._pMagic=40;player._pDexterity=750;player._pVitality=750;
 minDamage=1000;maxDamage=2000;
 DrawChr3DS({});checkValues();
 bool red=false,white=false;for(const auto&d:draws){if(d.rect.position==Point{132,106})red=HasAnyOf(d.flags,UiFlags::ColorDialogRed);if(d.rect.position==Point{132,130})white=HasAnyOf(d.flags,UiFlags::ColorDialogWhite);}assert(red&&white);
 // Current and maximum resources remain distinct; NoMana hides unusable mana.
 assert((*panelEntries[22].statDisplayFunc)().text=="1000");
 assert((*panelEntries[24].statDisplayFunc)().text=="500");
 player._pIFlags=ItemSpecialEffect::NoMana;
 assert((*panelEntries[23].statDisplayFunc)().text=="0");
 assert((*panelEntries[24].statDisplayFunc)().text=="0");
}
'''
# Move test bodies after the extracted production definitions.
pos=prelude.index('void checkValues()');tests=prelude[pos:];prelude=prelude[:pos]
array=source[source.index('PanelEntry panelEntries[] = {'):source.index('\nOptionalOwnedClxSpriteList Panel;')]
code=prelude+'\n'+'\n'.join(function(s) for s in ('UiFlags GetBaseStatColor(','UiFlags GetCurrentStatColor(','UiFlags GetValueColor(','UiFlags GetMaxManaColor(','UiFlags GetMaxHealthColor(','StyledText GetResistInfo('))+array+'\n'+function('void DrawChr3DS(')+'\n'+tests
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++20','-fsanitize=address,undefined','-I'+str(root/'Source'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: production character stat bindings, positive/negative bonuses, combat fields, resistances, max life/mana and large-number widths (ASan/UBSan)')
