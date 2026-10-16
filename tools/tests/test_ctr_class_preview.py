#!/usr/bin/env python3
"""Exercise menu-only class clips, one-shot actions, and stable frame anchors."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'Source/DiabloUI/hero/selhero.cpp').read_text()
def function(signature):
    start = source.index(signature)
    opening = source.index('\n{', start) + 1
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

code = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>
#include "platform/ctr/pixel_geometry.hpp"
#include "engine/point.hpp"
using namespace devilution;
struct Sprite {
 int w,h;std::vector<uint8_t> pixels;
 int width() const{return w;} int height() const{return h;}
};
using ClxSprite=Sprite;
struct ClxSpriteList {
 std::vector<Sprite> frames;
 uint32_t numSprites() const{return frames.size();}
 Sprite operator[](size_t i) const{return frames.at(i);}
};
using OwnedClxSpriteList=ClxSpriteList;
struct Rect {int x,y,w,h;};
Rect MakeSdlRect(int x,int y,int w,int h){return{x,y,w,h};}
struct OwnedSurface {
 int width,height;std::vector<uint8_t> pixels;OwnedSurface *surface=this;
 OwnedSurface(int w,int h):width(w),height(h),pixels(w*h){}
 uint8_t *at(int x,int y){assert(x>=0&&x<width&&y>=0&&y<height);return &pixels[y*width+x];}
 void ScaleBlitFrom(OwnedSurface &src,Rect s,Rect d){
  for(int y=0;y<d.h;y++)for(int x=0;x<d.w;x++)
   *at(d.x+x,d.y+y)=*src.at(s.x+x*s.w/d.w,s.y+y*s.h/d.h);
 }
};
void SDL_FillSurfaceRect(OwnedSurface *s,void*,int color){std::fill(s->pixels.begin(),s->pixels.end(),color);}
void RenderClxSprite(OwnedSurface &out,Sprite sprite,Point p){
 for(int y=0;y<sprite.h;y++)for(int x=0;x<sprite.w;x++)
  if(sprite.pixels[y*sprite.w+x]) *out.at(p.x+x,p.y+y)=sprite.pixels[y*sprite.w+x];
}
OwnedClxSpriteList SurfaceToClx(OwnedSurface &s,int frames,int){
 OwnedClxSpriteList result;int h=s.height/frames;
 for(int i=0;i<frames;i++)result.frames.push_back({s.width,h,{s.pixels.begin()+i*s.width*h,s.pixels.begin()+(i+1)*s.width*h}});
 return result;
}
struct ClassPreviewClip {
 std::optional<OwnedClxSpriteList> raw,mapped;uint32_t idleFrames=0,actionFrames=0;bool attempted=false;
};
''' + '\n'.join(function(s) for s in ('size_t FirstClassPreview(', 'uint32_t ClassPreviewFrame(', 'void PrepareClassPreview(')) + r'''
Sprite solid(int w,int h,uint8_t color){return{w,h,std::vector<uint8_t>(w*h,color)};}
int bottom(const Sprite &s){for(int y=s.h-1;y>=0;y--)for(int x=0;x<s.w;x++)if(s.pixels[y*s.w+x])return y;return -1;}
int main(){
 // Base classes always stay visible. Hellfire classes remain reachable in a
 // four-character window, with the current selection inside that window.
 for(size_t count=1;count<=6;count++)for(size_t selected=0;selected<count;selected++){
  size_t first=FirstClassPreview(selected,count);
  assert(first<=selected&&selected<first+std::min<size_t>(4,count));
  assert(first+std::min<size_t>(4,count)<=count);
  if(count<=4)assert(first==0);
 }
 // Unselected characters freeze at idle frame zero, including while an intro
 // runs on another class. Selected action plays once, then loops only idle.
 for(uint32_t t=0;t<5000;t+=10){
  assert(ClassPreviewFrame(t,4,6,false)==0);
  auto f=ClassPreviewFrame(t,4,6,true);
  if(t<480)assert(f==4+t/80);else assert(f<4);
 }
 assert(ClassPreviewFrame(0,4,6,true)==4); // switching class restarts its intro
 assert(ClassPreviewFrame(480,4,6,true)==0);
 assert(ClassPreviewFrame(580,4,6,true)==1);
 assert(ClassPreviewFrame(700,4,0,true)==3); // missing optional action
 assert(ClassPreviewFrame(700,0,0,true)==0);
 uint32_t started=0xffffffe0U,now=0x10U;
 assert(ClassPreviewFrame(now-started,4,6,true)==4);
 ClassPreviewClip clip;
 ClxSpriteList idle{{solid(10,20,1),solid(10,20,2)}};
 ClxSpriteList attack{{solid(32,24,3),solid(28,22,4)}};
 PrepareClassPreview(clip,idle,attack);
 assert(clip.raw&&clip.idleFrames==2&&clip.actionFrames==2);
 assert(clip.raw->numSprites()==4);
 auto first=(*clip.raw)[0];assert(first.w<=122&&first.h<=164);
 for(auto &f:clip.raw->frames){assert(f.w==first.w&&f.h==first.h);assert(bottom(f)==bottom(first));}
 ClassPreviewClip compact;PrepareClassPreview(compact,idle,attack,90);
 assert(compact.raw);
 for(auto &f:compact.raw->frames){assert(f.w<=90&&f.h<=164);assert(bottom(f)==bottom((*compact.raw)[0]));}
 ClassPreviewClip fallback;PrepareClassPreview(fallback,idle,std::nullopt);
 assert(fallback.raw&&fallback.actionFrames==0&&fallback.idleFrames==2);
 ClassPreviewClip invalid;PrepareClassPreview(invalid,ClxSpriteList{},attack);assert(!invalid.raw);
 PrepareClassPreview(invalid,ClxSpriteList{{solid(257,5,1)}},std::nullopt);assert(!invalid.raw);
}
'''
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined', '-I'+str(ROOT/'Source'), str(root/'test.cpp'), '-o', str(root/'test')], check=True)
    subprocess.run([str(root/'test')], check=True)
print('PASS: four-class Hellfire lineup, optional extra-class paging, frozen unselected clips, one-shot actions, idle loop, stable anchors, missing/invalid clips (ASan/UBSan)')

# The footer is limited to two 16px lines. Check shipped EN/LT glyph metrics;
# the production word-wrap routine is separately covered by ctr_multiline.
import ast
import re
import struct
translations = {}
for block in (ROOT/'Translations/lt.po').read_text().split('\n\n'):
    key = value = ''; target = None
    for line in block.splitlines():
        if line.startswith('msgid '): target='key'; key=ast.literal_eval(line[6:])
        elif line.startswith('msgstr '): target='value'; value=ast.literal_eval(line[7:])
        elif line.startswith('"'):
            if target=='key': key+=ast.literal_eval(line)
            elif target=='value': value+=ast.literal_eval(line)
    if key and value: translations[key]=value
names = re.findall(r'return N_\("([^"]+)"\)', function('const char *ClassPreviewDescription('))
assert len(names)==6
fonts = {}
def native_width(text):
    total = 0
    for ch in text:
        cp = ord(ch)
        if cp>>8 not in fonts: fonts[cp>>8]=(ROOT/f'assets/fonts/12-{cp>>8:02x}.clx').read_bytes()
        data=fonts[cp>>8];offset=struct.unpack_from('<I',data,4+4*(cp&255))[0]
        total+=struct.unpack_from('<H',data,offset+2)[0]
    return total + max(0,len(text)-1)
for name in names:
    assert name in translations
    for text in (name,translations[name]):
        lines=['']
        for word in text.split():
            trial=(lines[-1]+' '+word).strip()
            if native_width(trial)>375:
                assert lines[-1] and native_width(word)<=375
                lines.append(word)
            else: lines[-1]=trial
        assert len(lines)<=2 and len(lines)*16<=36, text
print('PASS: all six EN/LT class introductions fit the two-line footer with shipped native font metrics')
