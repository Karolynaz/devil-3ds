#!/usr/bin/env python3
"""Check the production menu sprite loader, fallback classes and missing assets."""
from pathlib import Path
import subprocess
import tempfile

source = Path('Source/player.cpp').read_text()
start = source.index('std::optional<OwnedClxSpriteList> LoadPlayerPreviewSprites(')
body = source[start:source.index('\n#endif', start)]
code = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
enum class HeroClass : uint8_t { Warrior, Rogue, Sorcerer, Monk, Bard, Barbarian, LAST=Barbarian };
enum class Direction { South=0 };
enum class player_graphic { Stand, Attack, Fire, Walk };
enum class PlayerWeaponGraphic : uint8_t {};
constexpr std::array ArmourChar {'l','m','h'};
constexpr std::array WepChar {'n','u','s','d','b','t','h','a','m'};
struct OwnedClxSpriteList { int translations=0; };
bool missing=false, sheet=true, empty=false;
int loads=0;
struct ClxSpriteList {
 uint32_t numSprites() const { return empty ? 0 : 8; }
 OwnedClxSpriteList clone() const { return {}; }
};
struct Sheet { ClxSpriteList operator[](size_t index) const { assert(index==0); return {}; } };
struct Loaded {
 bool isSheet() const { return ::sheet; }
 size_t numLists() const { return ::sheet ? 8 : 0; }
 Sheet sheet() const { return {}; }
};
struct Data { std::string classPath; char classChar; std::string trn; };
using PlayerSpriteData = Data;
Data data[]={{"warrior",'w',"warrior"},{"rogue",'r',"rogue"},{"sorceror",'s',"sorceror"},
             {"monk",'m',"monk"},{"rogue",'r',"bard"},{"warrior",'w',"barbarian"}};
HeroClass GetPlayerSpriteClass(HeroClass cls) { return cls==HeroClass::Bard ? HeroClass::Rogue : cls; }
const Data &GetPlayerSpriteDataForClass(HeroClass cls) { return data[static_cast<int>(cls)]; }
uint16_t GetPlayerSpriteWidth(HeroClass,player_graphic,PlayerWeaponGraphic) { return 96; }
std::string lastPath, classPath;
void GetPlayerGraphicsPath(std::string_view path,std::string_view prefix,std::string_view type,char *out) {
 lastPath=std::string(path)+"/"+std::string(prefix)+std::string(type); lastPath.copy(out,lastPath.size());out[lastPath.size()]=0;
}
template<class T>struct PointerOrValue { T value; };
std::expected<Loaded,std::string> LoadCl2ListOrSheetWithStatus(const char *,PointerOrValue<uint16_t>) {
 ++loads;if(missing)return std::unexpected("missing");return Loaded{};
}
std::optional<std::array<uint8_t,256>> GetPlayerGraphicTRN(const char *) { return std::array<uint8_t,256>{}; }
void ClxApplyTrans(OwnedClxSpriteList &sprites,const uint8_t *) { ++sprites.translations; }
template<class... T>std::string StrCat(T... value) { return (std::string{}+...+value); }
bool LoadOptionalFileInMem(const char *path,uint8_t *,size_t count) { assert(count==256);classPath=path;return true; }
''' + 'std::optional<OwnedClxSpriteList> LoadPlayerPreviewSprites(HeroClass, uint8_t, player_graphic = player_graphic::Stand);\n' + body + r'''
int main() {
 auto warrior=LoadPlayerPreviewSprites(HeroClass::Warrior,0x23);
 assert(warrior && warrior->translations==2 && lastPath=="warrior/whdas");
 auto bard=LoadPlayerPreviewSprites(HeroClass::Bard,0);
 assert(bard && lastPath=="rogue/rlnas" && classPath=="plrgfx\\bard.trn");
 auto attack=LoadPlayerPreviewSprites(HeroClass::Warrior,2,player_graphic::Attack);
 assert(attack && lastPath=="warrior/wlsat");
 auto cast=LoadPlayerPreviewSprites(HeroClass::Sorcerer,0,player_graphic::Fire);
 assert(cast && lastPath=="sorceror/slnfm");
 const int before=loads;
 assert(!LoadPlayerPreviewSprites(HeroClass::Warrior,0,player_graphic::Walk));
 assert(!LoadPlayerPreviewSprites(HeroClass::Warrior,0x30));
 assert(!LoadPlayerPreviewSprites(HeroClass::Warrior,0x0f));
 assert(!LoadPlayerPreviewSprites(static_cast<HeroClass>(255),0));
 assert(loads==before);
 missing=true;assert(!LoadPlayerPreviewSprites(HeroClass::Monk,0));missing=false;
 sheet=false;assert(!LoadPlayerPreviewSprites(HeroClass::Warrior,0));sheet=true;
 empty=true;assert(!LoadPlayerPreviewSprites(HeroClass::Warrior,0));
}
'''
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root/'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++23', '-fsanitize=address,undefined',
                    str(root/'test.cpp'), '-o', str(root/'test')], check=True)
    subprocess.run([str(root/'test')], check=True)
print('PASS: equipped appearance, class translations, fallback classes and nonfatal missing/invalid preview assets')
