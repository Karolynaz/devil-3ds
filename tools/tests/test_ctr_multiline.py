#!/usr/bin/env python3
"""Run production multiline layout against deterministic font metrics (no emulator)."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
source = Path(os.environ.get('TEXT_RENDER_SOURCE', ROOT/'Source/engine/render/text_render.cpp')).read_text()
def function(signature):
    start = source.index(signature)
    opening = source.index('\n{', start) + 1
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
STUBS = r'''
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>
#include "DiabloUI/ui_flags.hpp"
#include "engine/rectangle.hpp"
using namespace devilution;
struct Surface {};
enum class CtrTextScale { None, TopScreen, BottomScreen };
using GameFontTables = int; using text_color = int;
struct TextRenderOptions { UiFlags flags; int spacing=2; int lineHeight=30; };
constexpr char32_t ZWSP=0x200b, Utf8DecodeError=0xfffd;
struct Glyph { int width() const { return 7; } };
struct CurrentFont { bool load(int,int,char32_t) { return true; } Glyph glyph(uint8_t) { return {}; } };
[[noreturn]] void app_fatal(const char *) { std::abort(); }
char32_t DecodeFirstUtf8CodePoint(std::string_view s,size_t *n) { *n=1; return s[0]; }
int ScaleCharWidth(int w,CtrTextScale s) { return s==CtrTextScale::BottomScreen?w*2:s==CtrTextScale::TopScreen?w*8/5:w; }
int ScaleSpacing(int w,CtrTextScale s) { return s==CtrTextScale::BottomScreen?w*2:s==CtrTextScale::TopScreen?(w*8+2)/5:w; }
int GetLineWidth(std::string_view s,int,int spacing,int *count=nullptr,CtrTextScale scale=CtrTextScale::None) {
    auto end=s.find('\n'); if(end!=std::string_view::npos) s=s.substr(0,end);
    if(count) *count=s.size();
    return s.empty()?0:s.size()*ScaleCharWidth(7,scale)+(s.size()-1)*spacing;
}
struct Line { std::string_view text; int width; int y; };
std::vector<Line> lines;
void DrawLine(const Surface &,std::string_view s,Point p,Rectangle,UiFlags,int,int,int,bool,
    const TextRenderOptions &,size_t,int width,CtrTextScale) { lines.push_back({s,width,p.y}); }
'''
TEST = r'''
void check(bool condition) { if(!condition) std::abort(); }
int main() {
    // A nonzero newline glyph reproduces the real bug even with spacing=0.
    for (auto scale : {CtrTextScale::None,CtrTextScale::TopScreen,CtrTextScale::BottomScreen})
    for (auto flags : {UiFlags::AlignCenter,UiFlags::AlignRight,UiFlags::AlignCenter|UiFlags::KerningFitSpacing})
    for (int spacing : {0,1,2})
    for (auto text : {"Not Even Death\nCan Save You","Copyright 1996-2001\nBlizzard Entertainment","Long first line\nShort\nLast"}) {
        lines.clear();
        TextRenderOptions opts {flags,spacing,30};
        std::string_view s=text;
        const int effective=ScaleSpacing(spacing,scale);
        int count=0;
        int width=GetLineWidth(s,0,effective,&count,scale);
        Rectangle rect{{20,240},{600,240}};
        Point p{GetLineStartX(flags,rect,width),240};
        DoDrawString({},s,rect,p,width,count,620,480,0,0,false,opts,scale);
        size_t n=0;
        while (!s.empty()) {
            const auto end=s.find('\n');
            const auto line=s.substr(0,end);
            check(n<lines.size() && lines[n].text==line);
            const int expectedWidth=GetLineWidth(line,0,effective,nullptr,scale);
            check(lines[n].width==expectedWidth);
            const int x=GetLineStartX(flags,rect,lines[n].width);
            check(x==(HasAnyOf(flags,UiFlags::AlignRight)?620-expectedWidth:20+(600-expectedWidth)/2));
            check(lines[n].y==240+int(n)*30);
            ++n;
            if(end==std::string_view::npos) break;
            s.remove_prefix(end+1);
        }
        check(n==lines.size());
    }
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/'text.cpp'; exe=Path(directory)/'text'
    cpp.write_text(STUBS+'\n'+function('int AdjustSpacingToFitHorizontally(')+'\n'+function('int GetLineStartX(')+'\n'+function('uint32_t DoDrawString(')+'\n'+TEST)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-D__3DS__','-I'+str(ROOT/'Source'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: production multiline layout centers each line independently')
