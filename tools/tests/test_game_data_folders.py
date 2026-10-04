#!/usr/bin/env python3
"""Exercise production POSIX folder creation and 3DS write probe on the host."""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
def function(path, signature):
    s=(ROOT/path).read_text();start=s.index(signature);i=s.index('\n{',start)+1;j=i+1;depth=1
    while depth:
        depth+=(s[j]=='{')-(s[j]=='}');j+=1
    return s[start:j]
CPP=r"""
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <sys/stat.h>
constexpr char DirectorySeparator='/';
#define DIRECTORY_SEPARATOR_STR "/"
template<class... T> void LogError(T&&...) {}
bool DirectoryExists(const char *p) { struct stat st;return stat(p,&st)==0 && S_ISDIR(st.st_mode); }
using SDL_IOStream=FILE;
SDL_IOStream *SDL_IOFromFile(const char *p,const char *mode) { return fopen(p,mode); }
void SDL_CloseIO(FILE *p) { fclose(p); }
void RemoveFile(const char *p) { std::remove(p); }
std::string pref;
namespace paths { const std::string &PrefPath() { return pref; } }
[[noreturn]] void DirErrorDlg(const std::string &) { throw 1; }
"""
TEST=r"""
int main(int argc,char **argv) {
    assert(argc==2);
    pref=std::string(argv[1])+"/new/3ds/devilutionx/";
    ReadOnlyTest();
    assert(DirectoryExists(pref.c_str()));
    assert(!std::filesystem::exists(pref+"Diablo1ReadOnlyTest.foo"));
    ReadOnlyTest(); // Existing folders must succeed, including mkdir races.
    assert(CreateDir(pref.c_str()));
    const std::string file=std::string(argv[1])+"/blocked";
    FILE *f=fopen(file.c_str(),"w");assert(f);fclose(f);
    assert(!CreateDir(file.c_str()));
    pref=file+"/3ds/devilutionx/";
    try { ReadOnlyTest();assert(false); } catch(int) {}
    assert(std::filesystem::is_regular_file(file));
}
"""
with tempfile.TemporaryDirectory() as d:
    p=Path(d); cpp=p/'folders.cpp';exe=p/'folders'
    cpp.write_text(CPP+'\n'.join(function(path,sig) for path,sig in [
        ('Source/utils/file_util.cpp','std::string_view Dirname('),
        ('Source/utils/file_util.cpp','bool CreateDir(const char *path)'),
        ('Source/utils/file_util.cpp','void RecursivelyCreateDir('),
        ('Source/restrict.cpp','void ReadOnlyTest()')])+TEST)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-D__3DS__','-fsanitize=address,undefined',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe),d],check=True)
print('PASS: missing and existing save folders; blocked paths; write probe cleanup (ASan/UBSan)')
