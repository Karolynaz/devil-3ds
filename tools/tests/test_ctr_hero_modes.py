#!/usr/bin/env python3
"""Exercise production mode/slot separation and per-save intro consumption."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
def function(file,signature):
 s=(ROOT/file).read_text();a=s.index(signature);b=s.index('\n{',a)+1;c=b+1;depth=1
 while depth:
  depth+=(s[c]=='{')-(s[c]=='}');c+=1
 return s[a:c]
code=r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#define __3DS__ 1
constexpr int MAX_CHARACTERS=99;
bool gbIsHellfire=false,gbIsHellfireSaveGame=false;
uint8_t giNumberOfLevels=17;
struct Options{struct Mods{int reloads=0;void SetHellfireEnabled(bool enabled){reloads++;gbIsHellfire=enabled;}}Mods;}options;
Options &GetOptions(){return options;}
bool available=true,held=false;
bool HeroListActive=true,selhero_isSavegame=false;
struct SDL_Event {int button;bool up;};
struct ControllerButtonEvent {int button;bool up;};
std::vector<ControllerButtonEvent> ToControllerButtonEvents(SDL_Event &event) {return {{event.button,event.up}};}
bool HaveHellfire(){return available;}
constexpr int ControllerButton_BUTTON_LEFTSHOULDER=7;
bool IsControllerButtonPressed(int key){assert(key==7);return held;}
struct _uiheroinfo{uint32_t saveNumber;bool hellfire;};
_uiheroinfo selhero_heros[2*MAX_CHARACTERS];size_t selhero_SaveCount=0;
struct Slot {bool exists=false,hasMarker=false,readError=false;std::vector<std::byte> data;};
Slot slots[2][MAX_CHARACTERS];
int readers=0,movies=0;
struct SaveReader{
 Slot *slot;SaveReader(Slot*s):slot(s){readers++;}
 SaveReader(const SaveReader &)=delete;
 SaveReader(SaveReader&&other):slot(other.slot){other.slot=nullptr;}
 ~SaveReader(){if(slot)readers--;}
 bool HasFile(const char *name){assert(std::string(name)=="hfintro");return slot->hasMarker;}
 std::unique_ptr<std::byte[]>ReadFile(const char*,size_t&size,int32_t&error){
  error=slot->readError?1:0;size=slot->data.size();auto data=std::make_unique<std::byte[]>(size);
  if(size)std::memcpy(data.get(),slot->data.data(),size);return data;
 }
};
std::optional<SaveReader>OpenSaveArchive(uint32_t id){
 auto &slot=slots[gbIsHellfire][id];if(!slot.exists)return std::nullopt;return std::optional<SaveReader>(std::in_place,&slot);
}
struct SaveWriter{
 Slot *slot;bool WriteFile(const char*,const std::byte *data,size_t size){slot->data.assign(data,data+size);slot->hasMarker=true;return true;}
};
SaveWriter GetSaveWriter(uint32_t id){assert(readers==0);return {&slots[gbIsHellfire][id]};}
void play_movie(const char*path,bool closable){assert(readers==0&&gbIsHellfire&&closable);assert(std::string(path)=="gendata\\Hellfire.smk");movies++;}
'''
code+='\n'.join(function(f,s) for f,s in [
 ('Source/game_mode.cpp','void SetCtrHeroGameMode(bool hellfire)'),
 ('Source/DiabloUI/hero/selhero.cpp','bool CreatingDiabloHero()'),
 ('Source/DiabloUI/hero/selhero.cpp','bool ConsumeHeroModeModifierEvent('),
 ('Source/DiabloUI/hero/selhero.cpp','bool SelHeroGetHeroInfo(_uiheroinfo *pInfo)'),
 ('Source/pfile.cpp','void pfile_show_pending_hellfire_intro(uint32_t saveNum)')])
code+=r'''
int main(){
 // L down and up never reach the paging handler while New Hero is focused.
 SDL_Event modifier{7,false},release{7,true},select{1,false};
 assert(ConsumeHeroModeModifierEvent(modifier));
 assert(ConsumeHeroModeModifierEvent(release));
 assert(!ConsumeHeroModeModifierEvent(select));
 held=true;assert(CreatingDiabloHero());held=false;assert(!CreatingDiabloHero());
 selhero_isSavegame=true;assert(!ConsumeHeroModeModifierEvent(modifier));selhero_isSavegame=false;
 HeroListActive=false;assert(!ConsumeHeroModeModifierEvent(modifier));HeroListActive=true;
 available=false;assert(!ConsumeHeroModeModifierEvent(modifier));available=true;

 SetCtrHeroGameMode(false);assert(options.Mods.reloads==0&&giNumberOfLevels==17&&!gbIsHellfireSaveGame);
 SetCtrHeroGameMode(true);assert(options.Mods.reloads==1&&giNumberOfLevels==25&&gbIsHellfireSaveGame);
 SetCtrHeroGameMode(true);assert(options.Mods.reloads==1);
 SetCtrHeroGameMode(false);assert(options.Mods.reloads==2&&giNumberOfLevels==17&&!gbIsHellfireSaveGame);
 for(bool files:{false,true})for(bool l:{false,true}){available=files;held=l;assert(CreatingDiabloHero()==(!files||l));}
 _uiheroinfo base{0,false},expansion{0,true};
 SelHeroGetHeroInfo(&base);SelHeroGetHeroInfo(&expansion);
 assert(selhero_heros[0].saveNumber==selhero_heros[1].saveNumber&&selhero_heros[0].hellfire!=selhero_heros[1].hellfire);
 for(int i=2;i<2*MAX_CHARACTERS;i++)assert(SelHeroGetHeroInfo(&base));
 assert(!SelHeroGetHeroInfo(&base)&&selhero_SaveCount==2*MAX_CHARACTERS);
 slots[0][0]={true,true,false,{std::byte{1}}};slots[1][0]={true,true,false,{std::byte{1}}};
 pfile_show_pending_hellfire_intro(0);assert(movies==0&&slots[0][0].data[0]==std::byte{1});
 SetCtrHeroGameMode(true);
 pfile_show_pending_hellfire_intro(0);assert(movies==1&&slots[1][0].data[0]==std::byte{0});
 pfile_show_pending_hellfire_intro(0);assert(movies==1); // Same character, next launch.
 slots[1][1]={true,true,false,{std::byte{1}}};pfile_show_pending_hellfire_intro(1);assert(movies==2); // Independent character.
 slots[1][2]={true,false,false,{}};pfile_show_pending_hellfire_intro(2);assert(movies==2); // Old save.
 slots[1][3]={true,true,true,{std::byte{1}}};pfile_show_pending_hellfire_intro(3);assert(movies==2); // Read failure.
 slots[1][4]={true,true,false,{std::byte{1},std::byte{1}}};pfile_show_pending_hellfire_intro(4);assert(movies==2); // Invalid marker.
 pfile_show_pending_hellfire_intro(5);assert(movies==2); // Missing character.
 assert(slots[0][0].data[0]==std::byte{1}); // Diablo namespace never changed.
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'modes.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined',str(p/'modes.cpp'),'-o',str(p/'modes')],check=True)
 subprocess.run([str(p/'modes')],check=True)
print('PASS: L selection, mode/save namespace pairing, duplicate slots, list capacity and per-character intro exactly once (ASan/UBSan)')
