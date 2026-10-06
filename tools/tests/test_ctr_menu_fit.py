#!/usr/bin/env python3
"""Exercise production menu fitting with the shipped EN/LT bitmap fonts."""
import ast
import json
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    opening = source.index('\n{', start) + 1
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


translations = {}
for block in (ROOT / 'Translations/lt.po').read_text().split('\n\n'):
    key = value = ''; target = None
    for line in block.splitlines():
        if line.startswith('msgid '): target = 'key'; key = ast.literal_eval(line[6:])
        elif line.startswith('msgstr '): target = 'value'; value = ast.literal_eval(line[7:])
        elif line.startswith('"'):
            if target == 'key': key += ast.literal_eval(line)
            elif target == 'value': value += ast.literal_eval(line)
    if key and value: translations[key] = value

labels = ['Single Player', 'Multi Player', 'Settings', 'Credits', 'Exit Diablo', 'Exit Hellfire', 'ZeroTier', 'TCP/IP (LAN)', 'Select Connection', 'Multiplayer']
cases = labels + [translations.get(label, label) for label in labels]
chars = set(''.join(cases))
widths = []
for font, size in enumerate((12, 24, 30)):
    for char in chars:
        cp = ord(char)
        data = (ROOT / f'assets/fonts/{size}-{cp >> 8:02x}.clx').read_bytes()
        offset = struct.unpack_from('<I', data, 4 + 4 * (cp & 255))[0]
        widths.append((font, cp, struct.unpack_from('<H', data, offset + 2)[0]))

render = (ROOT / 'Source/engine/render/text_render.cpp').read_text()
ui = (ROOT / 'Source/DiabloUI/diabloui.cpp').read_text()
prelude = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>
#include "DiabloUI/ui_flags.hpp"
using namespace devilution;
enum GameFontTables { GameFont12, GameFont24, GameFont30, GameFont42, GameFont46, FontSizeDialog };
enum class CtrTextScale { BottomScreen };
enum class text_color { ColorDialogWhite };
constexpr char32_t ZWSP=0x200b, Utf8DecodeError=0xfffd;
std::unordered_map<unsigned,int> widths;
struct Glyph { int w; int width() const { return w; } };
struct CurrentFont {
 unsigned key;
 bool load(GameFontTables font,text_color,char32_t cp) { key=font*65536+cp;return widths.count(key); }
 Glyph glyph(uint8_t) { return {widths.at(key)}; }
};
struct Utf8CodePoints {
 std::u32string codepoints;
 explicit Utf8CodePoints(std::string_view s) {
  while(!s.empty()) {
   uint8_t first=s[0]; size_t n=first<128?1:first<224?2:first<240?3:4;
   char32_t cp=first&(n==1?127:n==2?31:n==3?15:7);
   for(size_t i=1;i<n;++i) cp=(cp<<6)|(uint8_t(s[i])&63);
   codepoints.push_back(cp);s.remove_prefix(n);
  }
 }
 auto begin() const {return codepoints.begin();} auto end() const {return codepoints.end();}
};
int ScaleSpacing(int w,CtrTextScale) { return w*2; }
int ScaleCharWidth(int w,CtrTextScale) { return w*2; }
[[noreturn]] void app_fatal(const char *) { std::abort(); }
'''
font = function((ROOT / 'Source/engine/render/text_render.hpp').read_text(), 'constexpr GameFontTables GetFontSizeFromUiFlags(')
measure = function(render, 'int GetLineWidth(std::string_view text, GameFontTables size, int spacing, int *charactersInLine, CtrTextScale scale)')
fit = function(ui, 'void FitCtrUiText(')
test = '\nint main() { widths = {' + ','.join('{' + str(f * 65536 + cp) + ',' + str(w) + '}' for f, cp, w in widths) + '};\n'
# Cover selector widths from 22 through 32 native pixels, with the production
# eight-logical-pixel gap at each end of the 510-wide menu.
for label in cases:
    test += '{ std::string label=' + json.dumps(label, ensure_ascii=False) + r''';
    UiFlags flags=UiFlags::FontSize24|UiFlags::ColorUiGold|UiFlags::AlignCenter;
    int spacing=4;
    for(int selectorWidth: {22,26,32}) {
    flags=UiFlags::FontSize24|UiFlags::ColorUiGold|UiFlags::AlignCenter;spacing=4;
    const int available=510-2*(selectorWidth*2+8);
    FitCtrUiText(label,available,CtrTextScale::BottomScreen,flags,spacing);
    assert(GetLineWidth(label,GetFontSizeFromUiFlags(flags),spacing,nullptr,CtrTextScale::BottomScreen)<=available);
    assert(HasAnyOf(flags,UiFlags::AlignCenter|UiFlags::ColorUiGold));
    assert(spacing>=0 && spacing<=4);
    }
    }''' + '\n'
test += r'''
    UiFlags flags=UiFlags::FontSize12;int spacing=0;
    FitCtrUiText("Single Player",1,CtrTextScale::BottomScreen,flags,spacing);
    assert(GetFontSizeFromUiFlags(flags)==GameFont12); // Never enlarge a small font.
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'menu.cpp'; exe = Path(directory) / 'menu'
    cpp.write_text(prelude + font + measure + fit + test)
    subprocess.run(['c++', '-std=c++17', '-I' + str(ROOT / 'Source'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print(f'PASS: all {len(cases)} complete EN/LT menu labels fit between selectors')
