#!/usr/bin/env python3
"""Exercise new/legacy 3DS folder selection against real folders and startup cache."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
code=r'''
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
namespace devilution {
int probes=0;
bool DirectoryExists(const char *p) {probes++;struct stat s;return stat(p,&s)==0&&S_ISDIR(s.st_mode);}
}
#include "platform/ctr/data_path.hpp"
using namespace devilution;
void check(bool v){if(!v)std::abort();}
int main(){
 const char *a="new/",*b="old/";
 check(std::string(ChooseCtrDataPath(a,b))==a);
 check(!std::filesystem::exists(a)&&!std::filesystem::exists(b));
 std::filesystem::create_directory(b);std::ofstream("old/diablo.ini")<<"old settings";std::ofstream("old/single_0.sv")<<"saved hero";
 check(std::string(ChooseCtrDataPath(a,b))==b);
 check(!std::filesystem::exists(a));
 std::filesystem::create_directory(a);check(std::string(ChooseCtrDataPath(a,b))==a);
 std::filesystem::remove(a);std::ofstream("new")<<"blocked";check(std::string(ChooseCtrDataPath(a,b))==b);
 std::filesystem::remove_all("new");std::filesystem::remove_all("old");
 // Same cached path feeds data/config/save calls, even if a folder is created later.
 std::filesystem::create_directories("sdmc:/3ds/devilutionx");
 const std::string first=CtrDataPath();check(first==CtrLegacyDataPath);
 const int before=probes;
 std::filesystem::create_directories("sdmc:/3ds/devil3ds");
 for(int i=0;i<10000;i++)check(CtrDataPath()==first);
 check(probes==before);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'utils').mkdir();(p/'utils/file_util.h').write_text('#pragma once\nnamespace devilution {bool DirectoryExists(const char*);}\n')
 (p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+d,'-I'+str(ROOT/'Source'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],cwd=d,check=True)
# All SDL entry points share the selector and neither config path probes cwd on 3DS.
back=(ROOT/'Source/utils/sdl2_to_1_2_backports.cpp').read_text()
assert back.count('SDL_strdup(devilution::CtrDataPath())')==2
assert 'file:sdmc:/3ds/devilutionx/' not in back
paths=(ROOT/'Source/utils/paths.cpp').read_text()
assert paths.count('#if !defined(__amigaos__) && !defined(__DJGPP__) && !defined(__3DS__)')==2
print('PASS: fresh/legacy/both/blocked folders, no migration, one cached selection for all data/config/save paths (ASan/UBSan)')
