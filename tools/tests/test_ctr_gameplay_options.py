#!/usr/bin/env python3
"""Run production grouped option/default/reset logic with minimal option storage."""
from pathlib import Path
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'Source/options.cpp').read_text()
header = (ROOT / 'Source/options.h').read_text()
def function(signature):
    start = source.index(signature)
    opening = source.index('\n{', start) + 1
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
fields = re.findall(r'\b(OptionEntryBoolean|OptionEntryInt<int>|OptionEntryEnum<StoreUi>)\s+(\w+);', header.split('struct GameplayOptions :')[1].split('struct ControllerOptions')[0])
prelude = r'''
#include <cassert>
#include <algorithm>
#include <string_view>
#include <vector>
#include <utility>
#define N_(s) s
#define __3DS__ 1
enum class OptionEntryFlags { None, Invisible, CantChangeInGame, OnlyHellfire, CantChangeInMultiPlayer };
OptionEntryFlags operator|(OptionEntryFlags a,OptionEntryFlags){return a;}
constexpr auto CtrGraphicsFlag=OptionEntryFlags::Invisible;
enum class StoreUi {Text,ListWithItemGraphics,VisualGrid};
enum class OptionEntryType {Boolean,List};
struct OptionEntryBase {
 std::string_view key;
 OptionEntryBase(std::string_view key):key(key){}
 virtual OptionEntryType GetType()const=0;
};
struct OptionEntryBoolean:OptionEntryBase {
 bool value;
 OptionEntryBoolean(std::string_view key,OptionEntryFlags,const char*,const char*,bool value):OptionEntryBase(key),value(value){}
 bool operator*()const{return value;}void SetValue(bool v){value=v;}
 OptionEntryType GetType()const override{return OptionEntryType::Boolean;}
};
struct OptionEntryListBase:OptionEntryBase {
 using OptionEntryBase::OptionEntryBase;
 OptionEntryType GetType()const override{return OptionEntryType::List;}
 virtual size_t GetActiveListIndex()const=0;virtual void SetActiveListIndex(size_t)=0;
};
template<class T>struct OptionEntryInt:OptionEntryListBase {
 T value;std::vector<T> values;
 OptionEntryInt(std::string_view key,OptionEntryFlags,const char*,const char*,T v,std::initializer_list<T> list={}):OptionEntryListBase(key),value(v),values(list){}
 T operator*()const{return value;}void SetValue(T v){value=v;}
 size_t GetActiveListIndex()const override{return std::find(values.begin(),values.end(),value)-values.begin();}
 void SetActiveListIndex(size_t i)override{value=values.at(i);}
};
template<class T>struct OptionEntryEnum:OptionEntryListBase {
 T value;std::vector<std::pair<T,const char*>> values;
 OptionEntryEnum(std::string_view key,OptionEntryFlags,const char*,const char*,T v,std::initializer_list<std::pair<T,const char*>> list):OptionEntryListBase(key),value(v),values(list){}
 T operator*()const{return value;}
 size_t GetActiveListIndex()const override{for(size_t i=0;i<values.size();i++)if(values[i].first==value)return i;return 0;}
 void SetActiveListIndex(size_t i)override{value=values.at(i).first;}
};
struct OptionCategoryBase {OptionCategoryBase(const char*,const char*,const char*){}};
struct GameplayOptions:OptionCategoryBase {
 GameplayOptions();std::vector<OptionEntryBase*> GetEntries();
'''
prelude += '\n'.join(f'{kind} {name};' for kind,name in fields)+'\n};\n'
prelude += '''struct EmptyOptions{std::vector<OptionEntryBase*>GetEntries(){return {};}};
struct Options {GameplayOptions Gameplay;EmptyOptions GameMode,Mods;};
Options &GetOptions(){static Options options;return options;}
'''
prelude += re.search(r'std::vector<OptionEntryBase \*> GetCtrGameplayEntries\(int group = -1\);', header)[0] + '\n'
code = prelude + '\n'.join(function(s) for s in (
 'GameplayOptions::GameplayOptions()',
 'std::vector<OptionEntryBase *> GameplayOptions::GetEntries()',
 'void ApplyCtrGameplayOptions()',
 'const char *CtrGameplayGroupName(int group)',
 'std::vector<OptionEntryBase *> GetCtrGameplayEntries(int group)',
 'void ResetCtrGameplayOptions(bool liveOnly)')) + r'''
int main(){
 auto &g=GetOptions().Gameplay;
 assert(*g.showSmallMap&&*g.autoEquip);
 assert(*g.storeUi==StoreUi::ListWithItemGraphics);
 ApplyCtrGameplayOptions();
 for(bool enabled:{true,false}){
  g.showVitalValues.SetValue(enabled);g.autoConsumables.SetValue(enabled);g.autoEquip.SetValue(enabled);
  ApplyCtrGameplayOptions();
  assert(*g.showHealthValues==enabled&&*g.showManaValues==enabled);
  for(auto *v:{&g.autoEquipWeapons,&g.autoEquipArmor,&g.autoEquipHelms,&g.autoEquipShields,&g.autoEquipJewelry,&g.autoElixirPickup,&g.autoOilPickup})assert(**v==enabled);
  for(auto *v:{&g.numHealPotionPickup,&g.numFullHealPotionPickup,&g.numManaPotionPickup,&g.numFullManaPotionPickup,&g.numRejuPotionPickup,&g.numFullRejuPotionPickup})assert(**v==(enabled?8:0));
 }
 auto quick=GetCtrGameplayEntries(-1);
 assert(quick.size()==8);
 for(auto *v:quick)assert(v!=&g.quickCast&&v!=&g.friendlyFire&&v!=&g.multiplayerFullQuests&&v!=&g.runInTown);
 for(int i=0;i<7;i++){
  assert(CtrGameplayGroupName(i)[0]);
  for(auto *v:GetCtrGameplayEntries(i))assert(v!=&g.quickCast);
 }
 g.runInTown.SetValue(true);g.friendlyFire.SetValue(true);g.randomizeQuests.SetValue(false);
 g.storeUi.SetActiveListIndex(2);g.showSmallMap.SetValue(false);
 g.tickRate.SetValue(40);
 ResetCtrGameplayOptions(true);
 assert(*g.autoEquip&&*g.showSmallMap&&*g.autoEquipJewelry);
 assert(*g.runInTown&&*g.friendlyFire&&! *g.randomizeQuests);
 assert(*g.storeUi==StoreUi::VisualGrid&&*g.tickRate==40);
 ResetCtrGameplayOptions(false);
 assert(! *g.runInTown&&! *g.friendlyFire&&*g.randomizeQuests);
 assert(*g.storeUi==StoreUi::ListWithItemGraphics&&*g.tickRate==40);
 assert(*g.experienceBar);
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory);(p/'options.cpp').write_text(code)
    subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined',str(p/'options.cpp'),'-o',str(p/'options')],check=True)
    subprocess.run([str(p/'options')],check=True)
print('PASS: shared health/mana, consumables, equipment, quick menu, groups and safe live/title reset (ASan/UBSan)')
