#!/usr/bin/env python3
"""Exercise real decoder-open and NPC speech failure paths without game data."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


sounds = (ROOT / 'Source/utils/soundsample.cpp').read_text()
effects = (ROOT / 'Source/effects.cpp').read_text()
text = (ROOT / 'Source/minitext.cpp').read_text()
prefix = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
bool decoderWorks=true,streamWorks=true,ioWorks=true,loadWorks=true;
int handles=0,plays=0,errors=0,duration=5000;
struct SDL_IOStream {};
void SDL_CloseIO(SDL_IOStream *h) { assert(h && handles>0);--handles;delete h; }
SDL_IOStream *makeIO() { if(!ioWorks)return nullptr;++handles;return new SDL_IOStream; }
SDL_IOStream *OpenAssetAsSdlRwOps(const char *,bool) { return makeIO(); }
SDL_IOStream *SDL_IOFromConstMem(const void *,int) { return makeIO(); }
const char *SDL_GetError() { return "bad WAV"; }
enum class LogCategory { Audio };
template<class... Args> void LogError(LogCategory,const char *,Args&&...) { ++errors; }
namespace Aulib {
struct Decoder { bool open(SDL_IOStream *) { return decoderWorks; } int getRate() { return 22050; } };
struct Resampler {};
struct Stream {
 SDL_IOStream *io;
 Stream(SDL_IOStream *h,std::unique_ptr<Decoder>,std::unique_ptr<Resampler>,bool close):io(h) { assert(close); }
 ~Stream() { SDL_CloseIO(io); }
 bool open() { return streamWorks; }
};
}
std::unique_ptr<Aulib::Decoder> CreateDecoder(bool) { return std::make_unique<Aulib::Decoder>(); }
std::unique_ptr<Aulib::Resampler> CreateAulibResampler(int) { return std::make_unique<Aulib::Resampler>(); }
template<class T> using ArraySharedPtr=std::shared_ptr<T[]>;
class SoundSample {
public:
 ArraySharedPtr<std::uint8_t> file_data_;
 std::size_t file_data_size_=0;
 std::string file_path_;
 bool isMp3_=false;
 std::unique_ptr<Aulib::Stream> stream_;
 int SetChunkStream(std::string,bool,bool);
 int SetChunk(ArraySharedPtr<std::uint8_t>,std::size_t,bool);
};
'''
mid = r'''
constexpr bool AllowStreaming=true;
constexpr int sfx_STREAM=1,VOLUME_MIN=-1600,VOLUME_MAX=0;
struct Sound { bool IsLoaded() { return true; } int GetLength() { return duration; }
 void PlayWithVolumeAndPan(int,int,int) { ++plays; } };
struct TSnd { Sound DSB; bool isPlaying() { return true; } };
struct TSFX { int bFlags=sfx_STREAM;std::string pszName="speech.wav";std::unique_ptr<TSnd> pSnd; };
struct LoadResult {
 bool ok;std::unique_ptr<TSnd> sound;
 bool has_value() { return ok; }
 std::string error() { return "missing speech"; }
 std::unique_ptr<TSnd> value() && { return std::move(sound); }
};
LoadResult SoundFileLoadWithStatus(const char *,bool) { return {loadWorks,loadWorks?std::make_unique<TSnd>():nullptr}; }
int sound_get_or_set_sound_volume(int) { return 0; }
std::vector<TSFX> sgSFX;
TSFX *sgpStreamSFX=nullptr;
bool gbSndInited=true;
void stream_stop();
enum class SfxID:int16_t { None=-1,Speech=0,Invalid=30 };
SfxID RndSFX(SfxID id) { return id; }
struct Point { int x,y; };
void PlaySfxPriv(TSFX *,bool,Point) { ++plays; }
constexpr int LineHeight=38;
std::vector<std::string> TextLines {"NPC speech"};
'''
suffix = r'''
int main() {
 auto buffer=ArraySharedPtr<std::uint8_t>(new std::uint8_t[16]);
 // A decoder rejecting corrupt MP3/WAV used to produce a null Stream.
 for(bool streamed : {false,true}) for(int failure=0;failure<4;failure++) {
  decoderWorks=failure!=0;streamWorks=failure!=1;ioWorks=failure!=2;
  SoundSample sample;
  int result=streamed?sample.SetChunkStream("bad.wav",false,true):sample.SetChunk(buffer,16,false);
  assert((result==0)==(failure==3));
  if(failure!=3) assert(!sample.stream_ && handles==0);
 }
 assert(handles==0);
 sgSFX.emplace_back();loadWorks=false;
 assert(GetSFXLength(SfxID::Speech)==0);
 StreamPlay(&sgSFX[0],0,0);assert(!sgpStreamSFX && plays==0);
 // Text still scrolls when audio fails, or the audio device is unavailable.
 assert(CalculateTextSpeed(SfxID::Speech)>0);
 gbSndInited=false;assert(GetSFXLength(SfxID::Speech)==0);gbSndInited=true;
 assert(GetSFXLength(SfxID::None)==0 && GetSFXLength(SfxID::Invalid)==0);
 PlaySFX(SfxID::None);PlaySFX(SfxID::Invalid);assert(plays==0);
 loadWorks=true;assert(GetSFXLength(SfxID::Speech)==5000);
 StreamPlay(&sgSFX[0],0,0);assert(sgpStreamSFX && plays==1);
 // Audio settings/cleanup can discard a voice still referenced by the stream.
 sgSFX[0].pSnd.reset();StreamUpdate();assert(!sgpStreamSFX);
 duration=0;assert(CalculateTextSpeed(SfxID::Speech)>0);
 duration=-1;assert(CalculateTextSpeed(SfxID::Speech)>0);
 TextLines.assign(10000,"long translated speech");duration=1;
 assert(CalculateTextSpeed(SfxID::Speech)==1);
}
'''
code = prefix + function(sounds, 'std::unique_ptr<Aulib::Stream> CreateStream') + '\n'
code += function(sounds, 'int SoundSample::SetChunkStream') + '\n'
code += function(sounds, 'int SoundSample::SetChunk(') + '\n' + mid
for signature in ['bool LoadSpeechSound(', 'void StreamPlay(', 'void StreamUpdate(', 'void stream_stop()', 'void PlaySFX(', 'int GetSFXLength(']:
    code += function(effects, signature) + '\n'
code += function(text, 'uint32_t CalculateTextSpeed(') + suffix
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('PASS: damaged/missing NPC voices, decoder and device failures, cleared streams and text-scroll fallback (ASan/UBSan)')
