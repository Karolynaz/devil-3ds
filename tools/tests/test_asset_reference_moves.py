#!/usr/bin/env python3
"""Verify that production asset moves retain override provenance and stream ownership."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
header = (ROOT / 'Source/engine/assets.hpp').read_text()
source = (ROOT / 'Source/engine/assets.cpp').read_text()
def definition(text, signature, after=0):
    start = text.index(signature, after)
    opening = text.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    if text[end:end+1] == ';': end += 1
    return text[start:end]

cpp = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <utility>
using SDL_IOStream=FILE;
int closed=0;
void SDL_CloseIO(FILE *f) { ++closed; assert(fclose(f)==0); }
const char *SDL_GetError() { return "IO error"; }
long SDL_GetIOSize(FILE *) { return 42; }
struct MpqArchive {
    size_t GetFileSizeFromHash(uint32_t) { return 42; }
    size_t GetFileSize(std::string_view) { return 42; }
};
struct AssetHandle {};
bool IsAssetIntegrityViolated=false;
'''
cpp += definition(header, 'struct AssetRef {', header.index('#else\nstruct AssetRef'))
cpp += '\nAssetHandle OpenAsset(AssetRef &&,bool) { return {}; }\n'
cpp += definition(source, 'AssetHandle OpenIntegralAsset(AssetRef &&ref, bool threadsafe)')
cpp += r'''
int main() {
    {
        AssetRef original; original.directHandle=tmpfile(); assert(original.directHandle);
        original.isOverridden=true; original.filename="custom.tsv";
        AssetRef moved(std::move(original));
        assert(!original.directHandle && moved.directHandle && moved.isOverridden);
        assert(moved.filename=="custom.tsv");
        OpenIntegralAsset(std::move(moved),false); assert(IsAssetIntegrityViolated);
        AssetRef assigned; assigned.directHandle=tmpfile(); assert(assigned.directHandle);
        assigned=std::move(moved);
        assert(closed==1 && !moved.directHandle && assigned.directHandle && assigned.isOverridden);
        IsAssetIntegrityViolated=false;
        OpenIntegralAsset(std::move(assigned),false); assert(IsAssetIntegrityViolated);
    }
    assert(closed==2);
    AssetRef target; target.isOverridden=true;
    AssetRef bundled; bundled.isOverridden=false;
    target=std::move(bundled);
    assert(!target.isOverridden);
    IsAssetIntegrityViolated=false;
    OpenIntegralAsset(std::move(target),false); assert(!IsAssetIntegrityViolated);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'moves.cpp'; exe = Path(directory) / 'moves'
    path.write_text(cpp)
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', str(path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: asset moves preserve integrity flags, replace old handles and close each stream once (ASan/UBSan)')
