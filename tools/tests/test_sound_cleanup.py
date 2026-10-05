#!/usr/bin/env python3
"""Check real sound finish callbacks after samples move to the cleanup list."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'Source/engine/sound.cpp').read_text()


def function(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


prefix = r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
namespace Aulib { struct Stream {}; }
using SdlMutex=std::mutex;
bool finishWhileDraining=false;
int destroyed=0;
struct SoundSample {
 std::function<void(Aulib::Stream &)> callback;
 int DuplicateFrom(const SoundSample &) { return 0; }
 void SetFinishCallback(std::function<void(Aulib::Stream &)> fn) { callback=std::move(fn); }
 ~SoundSample() {
  if(finishWhileDraining && callback) { Aulib::Stream stream;callback(stream); }
  ++destroyed;
 }
};
std::list<std::unique_ptr<SoundSample>> duplicateSounds;
std::optional<SdlMutex> duplicateSoundsMutex;
'''
suffix = r'''
int main() {
 duplicateSoundsMutex.emplace();
 SoundSample original;
 auto *sample=DuplicateSound(original);
 // Natural completion still removes exactly the matching sample.
 auto finish=sample->callback;Aulib::Stream stream;finish(stream);
 assert(duplicateSounds.empty() && destroyed==1);
 for(int i=0;i<100;i++) DuplicateSound(original);
 finishWhileDraining=true;
 // Simulate finish callbacks after the list move, before stream destruction.
 ClearDuplicateSounds();assert(duplicateSounds.empty() && destroyed==101);
 ClearDuplicateSounds();assert(destroyed==101);
 duplicateSoundsMutex.reset();
}
'''
code = prefix + function('SoundSample *DuplicateSound(') + '\n' + function('void ClearDuplicateSounds()') + suffix
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('PASS: audio finish callbacks during cleanup cannot erase an iterator from a different list (ASan/UBSan)')
