#!/usr/bin/env python3
"""Check compact settings with production wrapping and shipped EN/LT font widths."""
import ast
import json
import re
import struct
import subprocess
import tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
source = (ROOT/'Source/engine/render/text_render.cpp').read_text()
start = source.index('std::string WordWrapString(std::string_view text, unsigned width, GameFontTables size, int spacing, CtrTextScale scale)')
opening = source.index('\n{', start) + 1
end, depth = opening + 1, 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
production = source[start:end]
translations = {}
for block in (ROOT/'Translations/lt.po').read_text().split('\n\n'):
    key = value = ''; target = None
    for line in block.splitlines():
        if line.startswith('msgid '): target = 'key'; key = ast.literal_eval(line[6:])
        elif line.startswith('msgstr '): target = 'value'; value = ast.literal_eval(line[7:])
        elif line.startswith('"'):
            if target == 'key': key += ast.literal_eval(line)
            elif target == 'value': value += ast.literal_eval(line)
    if key and value: translations[key] = value
options = (ROOT/'Source/options.cpp').read_text().split('GameplayOptions::GameplayOptions()',1)[1].split('GameplayOptions::GetEntries()',1)[0]
labels = []
for line in options.splitlines():
    match = re.search(r'^\s*,\s*(\w+)\(.*?N_\("([^"]+)"\)',line)
    if match:
        labels.append((match[2], 260 if match[1] == 'storeUi' else 348))
labels += [('Text-only list',192),('List with item graphics',192),('Visual grid',192),('On',104),('Off',104)]
cases = [(translations.get(label,label) if language=='lt' else label,width) for language in ['en','lt'] for label,width in labels]
chars = set(''.join(label for label,_ in cases))
widths = {}
for ch in chars:
    cp = ord(ch); data = (ROOT/f'assets/fonts/12-{cp >> 8:02x}.clx').read_bytes()
    offset = struct.unpack_from('<I',data,4+4*(cp&255))[0]
    widths[cp] = struct.unpack_from('<H',data,offset+2)[0]
prelude = r'''
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
using GameFontTables=int;
enum class CtrTextScale { BottomScreen };
enum class text_color { ColorDialogWhite };
constexpr char32_t ZWSP=0x200b, Utf8DecodeError=0xfffd;
std::unordered_map<char32_t,int> widths;
struct Glyph { int w; int width() const {return w;} };
struct CurrentFont { char32_t cp; bool load(int,text_color,char32_t c) {cp=c;return widths.count(c);} Glyph glyph(uint8_t) {return {widths.at(cp)};} };
[[noreturn]] void app_fatal(const char *) {std::abort();}
int ScaleSpacing(int w,CtrTextScale) {return 2*w;}
int ScaleCharWidth(int w,CtrTextScale) {return 2*w;}
char32_t DecodeFirstUtf8CodePoint(std::string_view s,size_t *n) {
 auto c=uint8_t(s[0]); *n=c<128?1:c<224?2:c<240?3:4;
 char32_t cp=c&(*n==1?127:*n==2?31:*n==3?15:7);
 for(size_t i=1;i<*n;++i) cp=(cp<<6)|(uint8_t(s[i])&63);
 return cp;
}
bool IsBreakableWhitespace(char32_t c) {return c==' ';}
bool IsBreakAllowed(char32_t,char32_t) {return false;}
'''
main = '\nint main() { widths = {'+','.join('{'+str(cp)+','+str(w)+'}' for cp,w in widths.items())+'};\n'
for label,width in cases:
    literal = json.dumps(label,ensure_ascii=False)
    main += f'''{{ std::string label={literal}; auto wrapped=WordWrapString(label,{width},0,0,CtrTextScale::BottomScreen);
    int lines=1; for(char c:wrapped) if(c=='\\n') ++lines;
    if(lines>2) {{std::cerr<<label<<": "<<lines<<" lines\\n";return 1;}} }}\n'''
main += '}\n'
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/'settings.cpp'; exe=Path(directory)/'settings'
    cpp.write_text(prelude+production+main)
    subprocess.run(['c++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print(f'PASS: {len(cases)} EN/LT names and values fit two lines with shipped font metrics')
