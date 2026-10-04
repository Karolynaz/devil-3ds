#!/usr/bin/env python3
"""Check data-folder dialogs using production wrapping and shipped native font metrics."""
import ast
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'Source/engine/render/text_render.cpp').read_text()
def function(signature):
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

files = ['DIABDAT.MPQ', 'hellfire.mpq', 'hfmonk.mpq', 'hfmusic.mpq', 'hfvoice.mpq']
folder_message = 'Please create {:s}\nand put game data there.'
data_message = 'Copy to this SD card folder:\n{:s}\n\nRequired files:\n{:s}'
cases = []
for language in ('en', 'lt'):
    text = translations.get(folder_message, folder_message) if language == 'lt' else folder_message
    cases.append((text.replace('{:s}', '/3ds/devilutionx/', 1), []))
    for required in (files, files[1:], files[:1]):
        text = translations.get(data_message, data_message) if language == 'lt' else data_message
        text = text.replace('{:s}', 'sdmc:/3ds/devilutionx/', 1).replace('{:s}', '\n'.join(required), 1)
        cases.append((text, required))

dialogs = (ROOT / 'Source/DiabloUI/dialogs.cpp').read_text()
width = int(re.search(r'constexpr int TextWidth = (\d+)', dialogs)[1])
assert 'GameFont12, 1, CtrTextScale::TopScreen' in dialogs
assert 'MakeSdlRect(32, caption.empty() ? 20 : 48, TextWidth, caption.empty() ? 212 : 184)' in dialogs
assert 'ColorDialogWhite, 1, 16)' in dialogs
assert 'MakeSdlRect(224, 336, 192, 48)' in dialogs # Centered lower-screen touch target.
chars = set(''.join(text for text, _ in cases))
widths = {}
for ch in chars:
    cp = ord(ch)
    data = (ROOT / f'assets/fonts/12-{cp >> 8:02x}.clx').read_bytes()
    offset = struct.unpack_from('<I', data, 4 + 4 * (cp & 255))[0]
    widths[cp] = struct.unpack_from('<H', data, offset + 2)[0]

prelude = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>
using GameFontTables=int;
enum class CtrTextScale { None, TopScreen, BottomScreen };
enum class text_color { ColorDialogWhite };
constexpr char32_t ZWSP=0x200b, Utf8DecodeError=0xfffd;
std::unordered_map<char32_t,int> widths;
struct Glyph { int w; int width() const { return w; } };
struct CurrentFont {
    char32_t cp;
    bool load(int,text_color,char32_t c) { cp=c; return widths.count(c); }
    Glyph glyph(uint8_t) { return {widths.at(cp)}; }
};
[[noreturn]] void app_fatal(const char *) { std::abort(); }
char32_t DecodeFirstUtf8CodePoint(std::string_view s,size_t *n) {
    auto c=uint8_t(s[0]); *n=c<128?1:c<224?2:c<240?3:4;
    char32_t cp=c&(*n==1?127:*n==2?31:*n==3?15:7);
    for (size_t i=1;i<*n;++i) cp=(cp<<6)|(uint8_t(s[i])&63);
    return cp;
}
bool IsBreakableWhitespace(char32_t c) { return c==' '; }
bool IsBreakAllowed(char32_t,char32_t) { return false; }
'''
main = '\nint main() { widths = {' + ','.join(f'{{{cp},{w}}}' for cp, w in widths.items()) + '};\n'
for text, required in cases:
    main += f'{{ auto wrapped=WordWrapString({json.dumps(text, ensure_ascii=False)},{width},0,1,CtrTextScale::TopScreen);\n'
    main += "size_t lines=1; for(char c:wrapped) if(c=='\\n') ++lines; assert(lines*16<=184);\n"
    for file in required:
        main += f'assert(wrapped.find({json.dumps(file)})!=std::string::npos);\n'
    main += '}\n'
main += '}\n'
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'dialogs.cpp'; exe = Path(directory) / 'dialogs'
    production = '\n'.join(function(signature) for signature in (
        'inline int ScaleCharWidth(', 'inline int ScaleSpacing(',
        'std::string WordWrapString(std::string_view text, unsigned width, GameFontTables size, int spacing, CtrTextScale scale)'))
    cpp.write_text(prelude + production + main)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-fsanitize=address,undefined', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print(f'PASS: {len(cases)} EN/LT folder/archive messages fit the upper screen with complete file names (ASan/UBSan)')
