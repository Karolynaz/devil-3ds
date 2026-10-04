#!/usr/bin/env python3
"""Exercise production 3DS mod mounts, asset priority and Hellfire MPQ validation."""
from pathlib import Path
import os, re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
SOURCE=(ROOT/'Source/engine/assets.cpp').read_text()
def function(signature, after=0):
    start=SOURCE.index(signature, after);i=SOURCE.index('\n{',start)+1;j=i+1;depth=1
    while depth:
        depth+=(SOURCE[j]=='{')-(SOURCE[j]=='}');j+=1
    return SOURCE[start:j]
CPP=r"""
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#define DIRECTORY_SEPARATOR_STR "/"
struct MpqArchive {
    std::map<std::string,uint32_t> files;
    uint32_t FindHash(std::string_view name) const {
        auto i=files.find(std::string(name));return i==files.end()?UINT32_MAX:i->second;
    }
};
using MpqArchiveT=MpqArchive;
std::map<int,MpqArchiveT,std::greater<>> MpqArchives;
std::vector<std::string> OverridePaths,BundledModPaths;
struct ModManifest {std::string content;};
struct ModIdentifier {std::string name;bool whitelisted;ModManifest manifest;};
struct ModDependency {std::string name;std::vector<std::string> required;};
std::vector<ModIdentifier> ActiveModIdentifiers;
std::map<std::string,MpqArchive> available;
std::vector<std::string> loadCalls;
std::string base,pref,assets;
namespace paths {
const std::string &PrefPath(){return pref;}
const std::string &BasePath(){return base;}
const std::string &AssetsPath(){return assets;}
}
bool gbIsSpawn=false,gbIsHellfire=false,HeadlessMode=false,forceHellfire=false,HasHellfireMpq=false;
constexpr int MainMpqPriority=1000;
const char *SDL_GetError(){return "No archive";}
template<class... T> void LogError(T&&...) {}
std::string archiveError;
void InsertCDDlg(std::string_view s) {archiveError=s;}
void DisplayFatalErrorAndExit(std::string_view,std::string_view) { std::abort(); }
std::string_view _(std::string_view s) {return s;}
template<class... T> void LogVerbose(T&&...) {}
struct AssetRef {
    FILE *directHandle=nullptr;
    MpqArchive *archive=nullptr;
    uint32_t hashIndex=UINT32_MAX;
    std::string filename;
    bool isOverridden=false;
    AssetRef()=default;
    AssetRef(const AssetRef&)=delete;
    AssetRef(AssetRef&& o):directHandle(o.directHandle),archive(o.archive),hashIndex(o.hashIndex),filename(std::move(o.filename)),isOverridden(o.isOverridden) {o.directHandle=nullptr;}
    ~AssetRef(){if(directHandle)fclose(directHandle);}
};
FILE *SDL_IOFromFile(const char *p,const char *m){return fopen(p,m);}
FILE *OpenOptionalRWops(const std::string &p){return fopen(p.c_str(),"rb");}
FILE *OpenFile(const char *p,const char *m){return fopen(p,m);}
bool DirectoryExists(const std::string &p){return std::filesystem::is_directory(p);}
bool GetFileSize(const char *p,uintmax_t *s){*s=std::filesystem::file_size(p);return true;}
template<class... T> std::string StrCat(const T &...parts){std::string s;((s+=parts),...);return s;}
std::vector<std::string> GetMPQSearchPaths(){return {base,pref};}
ModManifest ParseModManifest(std::string_view s){return {std::string(s)};}
std::expected<ModManifest,int32_t> ReadPackedModManifest(std::span<const std::string>,std::string_view){return std::unexpected(0);}
std::vector<std::string> ReadPackedModRequiredMods(std::span<const std::string>,std::string_view){return {};}
std::vector<std::string> OrderModsByDependencies(const std::vector<ModDependency> &deps){std::vector<std::string> result;for(auto &d:deps)result.push_back(d.name);return result;}
void ClearModIdentifiers(){ActiveModIdentifiers.clear();}
void RegisterBuiltinModIdentifier(std::string_view n){ActiveModIdentifiers.push_back({std::string(n),true,{}});}
ModIdentifier &RegisterPackedModIdentifier(std::string_view n,const char*){ActiveModIdentifiers.push_back({std::string(n),false,{}});return ActiveModIdentifiers.back();}
void ReadLoadedModManifest(ModIdentifier &,int){}
bool LoadMPQ(std::span<const std::string>,std::string_view name,int priority,std::string_view=".mpq",std::string *loadedPath=nullptr){
    loadCalls.emplace_back(name);auto i=available.find(std::string(name));if(i==available.end())return false;
    MpqArchives.emplace(priority,i->second);if(loadedPath)*loadedPath=std::string(name);return true;
}
bool ContainsLogicAssets(const std::string &p,const std::string &,unsigned){
    if(!std::filesystem::is_directory(p))return false;
    for(auto &f:std::filesystem::recursive_directory_iterator(p))
        if(f.path().extension()==".tsv" || f.path().extension()==".lua")return true;
    return false;
}
"""
TEST=r"""
void write(std::string path,std::string_view content) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    FILE *f=fopen(path.c_str(),"wb");assert(f);fwrite(content.data(),1,content.size(),f);fclose(f);
}
std::string read(AssetRef &ref) {
    assert(ref.directHandle);char buf[1024];size_t n=fread(buf,1,sizeof(buf),ref.directHandle);return {buf,n};
}
void enableHF() {const std::string_view mod="hf";LoadModArchives(std::span(&mod,1));}
int main(int argc,char **argv) {
    assert(argc==2);
    base=std::string(argv[1])+"/base/";pref=std::string(argv[1])+"/sd/";assets=std::string(argv[1])+"/romfs/";
    const std::string table="txtdata/classes/classdat.tsv";
    const std::string manifest="saveExtension=hsv\nprogramId=HRTL\n";
    write(assets+table,"Diablo");write(assets+"mods/hf/"+table,"Hellfire");
    write(assets+"mods/hf/manifest.ini",manifest);
    write(assets+"mods/hf/lua/mods/hf/init.lua","hellfire.loadData()");
    MpqArchives[1000].files["txtdata\\classes\\classdat.tsv"]=17;
    enableHF();assert(BundledModPaths.size()==1);
    assert(ActiveModIdentifiers.size()==1 && ActiveModIdentifiers[0].whitelisted);
    assert(ActiveModIdentifiers[0].manifest.content==manifest);
    assert(!HasLooseLogicAssets());
    {auto ref=FindAsset("txtdata\\classes\\classdat.tsv");assert(!ref.isOverridden && !ref.archive);assert(read(ref)=="Hellfire");}
    {auto ref=FindAsset("lua\\mods\\hf\\init.lua");assert(!ref.isOverridden);assert(read(ref)=="hellfire.loadData()");}
    // Ordinary SD overrides and external packed mods retain their priority and integrity flags.
    write(pref+table,"User override");
    {auto ref=FindAsset("txtdata\\classes\\classdat.tsv");assert(ref.isOverridden);assert(read(ref)=="User override");}
    assert(HasLooseLogicAssets());std::filesystem::remove_all(pref);
    available["mods/hf"].files["txtdata\\classes\\classdat.tsv"]=91;
    UnloadModArchives();enableHF();assert(!ActiveModIdentifiers[0].whitelisted);
    {auto ref=FindAsset("txtdata\\classes\\classdat.tsv");assert(ref.archive && ref.hashIndex==91);}
    UnloadModArchives();assert(BundledModPaths.empty());
    {auto ref=FindAsset("txtdata\\classes\\classdat.tsv");assert(ref.archive && ref.hashIndex==17);}
    MpqArchives.clear();available.clear();loadCalls.clear();
    // Missing/unreadable hellfire.mpq must stop before looking up further archives.
    LoadHellfireArchives();assert(archiveError=="hellfire.mpq" && loadCalls.size()==1);
    loadCalls.clear();archiveError.clear();gbIsSpawn=true;
    LoadHellfireArchives();assert(archiveError=="DIABDAT.MPQ" && loadCalls.empty());gbIsSpawn=false;
    for(const auto *name:{"hellfire","hfmonk","hfmusic","hfvoice"})available[name]={};
    for(const auto *missing:{"hfmonk","hfmusic","hfvoice"}) {
        auto archive=available.extract(missing);MpqArchives.clear();archiveError.clear();
        LoadHellfireArchives();assert(!archiveError.empty());available.insert(std::move(archive));
    }
    MpqArchives.clear();archiveError.clear();LoadHellfireArchives();assert(archiveError.empty());
    assert(MpqArchives.size()==4);
    // An enabled Hellfire mod may precede base-archive discovery during Lua startup.
    available["spawn"]={};MpqArchives.clear();archiveError.clear();loadCalls.clear();
    gbIsHellfire=true;LoadGameArchives();
    assert(gbIsSpawn && archiveError=="DIABDAT.MPQ");
    assert(std::find(loadCalls.begin(),loadCalls.end(),"hfbard")==loadCalls.end());
    gbIsSpawn=false;gbIsHellfire=false;MpqArchives.clear();archiveError.clear();
    LoadGameArchives();assert(gbIsSpawn && archiveError.empty()); // Diablo shareware still works.
    available["DIABDAT"]={};gbIsSpawn=false;gbIsHellfire=true;MpqArchives.clear();archiveError.clear();
    LoadGameArchives();assert(!gbIsSpawn && archiveError.empty());
}
"""
with tempfile.TemporaryDirectory() as d:
    p=Path(d);cpp=p/'assets.cpp';exe=p/'assets'
    signatures = ['bool FindMpqFile(', 'AssetRef FindAsset(std::string_view filename)',
        'ModManifest ReadModManifestByName(', 'void UnloadModArchives()',
        'void LoadModArchives(', 'bool HasLooseLogicAssets()', 'void LoadHellfireArchives()',
        'void LoadGameArchives()']
    definitions = [function(signature, SOURCE.index('#else\nAssetRef FindAsset') if signature.startswith('AssetRef') else 0)
        for signature in signatures]
    cpp.write_text(CPP+'\n'.join(definitions)+TEST)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++23','-D__3DS__','-fsanitize=address,undefined',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe),d],check=True)
# Confirm the shipped source manifest and all CMake-declared Hellfire assets exist.
mods=(ROOT/'CMake/Mods.cmake').read_text().split('set(hellfire_mod',1)[1].split(')',1)[0].split()
for f in mods:assert (ROOT/'mods/hf'/f).is_file(),f
assert 'set(DEVILUTIONX_MODS_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/romfs/mods")' in (ROOT/'CMake/platforms/n3ds.cmake').read_text()
assert 'saveExtension=hsv' in (ROOT/'mods/hf/manifest.ini').read_text()
print('PASS: Hellfire bundled mounts, metadata, asset priority, integrity isolation and incomplete archives (ASan/UBSan)')
