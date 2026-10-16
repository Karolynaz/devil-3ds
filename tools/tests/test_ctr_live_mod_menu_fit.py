#!/usr/bin/env python3
"""Exercise the production 3DS in-game Mods label fitting with shipped EN/LT fonts."""
import ast
import json
import re
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
    key = value = ''
    target = None
    for line in block.splitlines():
        if line.startswith('msgid '):
            target = 'key'
            key = ast.literal_eval(line[6:])
        elif line.startswith('msgstr '):
            target = 'value'
            value = ast.literal_eval(line[7:])
        elif line.startswith('"'):
            if target == 'key':
                key += ast.literal_eval(line)
            elif target == 'value':
                value += ast.literal_eval(line)
    if key and value:
        translations[key] = value

options = (ROOT / 'Source/options.cpp').read_text()
gameplay = options.split('GameplayOptions::GameplayOptions()', 1)[1].split('GameplayOptions::GetEntries()', 1)[0]
entries = options.split('GameplayOptions::GetEntries()', 1)[1].split('\n}', 1)[0]
entry_names = re.findall(r'&([A-Za-z0-9_]+)', entries)
constructor = {}
for line in gameplay.splitlines():
    match = re.search(r',\s*(\w+)\("[^"]+",\s*(.*?),\s*(?:N_\("([^"]+)"\)|"([^"]*)"),', line)
    if match:
        constructor[match[1]] = (match[2], match[3] if match[3] is not None else match[4])

# These options are unavailable in the live menu because they either require
# a new game, use a desktop-only window/input path, or are exposed elsewhere.
excluded = {'tickRate', 'showSmallMap', 'grabInput', 'pauseOnFocusLoss',
            'runInTown', 'friendlyFire', 'multiplayerFullQuests',
            'randomizeQuests', 'theoQuest', 'cowQuest', 'testBard', 'testBarbarian'}
live_names = []
for entry in entry_names:
    if entry not in constructor or entry in excluded:
        continue
    flags, name = constructor[entry]
    if 'Invisible' in flags or 'CantChangeInGame' in flags:
        continue
    live_names.append(name)
assert live_names, 'No live Gameplay option names found in production declarations'

# Match each option with the values its production declaration can actually
# display: booleans, the six bounded potion counts, or Store UI's three modes.
values_by_entry = {
    'storeUi': ['Text-only list', 'List with item graphics', 'Visual grid'],
}
for entry in entry_names:
    if entry.startswith('num'):
        values_by_entry[entry] = ['0', '1', '2', '4', '8', '16']
    else:
        values_by_entry.setdefault(entry, ['On', 'Off'])
cases = []
for language in ('en', 'lt'):
    for entry in entry_names:
        if entry not in constructor or entry in excluded:
            continue
        flags, name = constructor[entry]
        if 'Invisible' in flags or 'CantChangeInGame' in flags:
            continue
        translated_name = translations.get(name, name) if language == 'lt' else name
        for value in values_by_entry[entry]:
            translated_value = translations.get(value, value) if language == 'lt' else value
            cases.append((translated_name, translated_value))

chars = set(''.join(name + value for name, value in cases) + '…: ')
widths = []
for font, size in enumerate((12, 24, 30)):
    for char in chars:
        cp = ord(char)
        data = (ROOT / f'assets/fonts/{size}-{cp >> 8:02x}.clx').read_bytes()
        offset = struct.unpack_from('<I', data, 4 + 4 * (cp & 255))[0]
        widths.append((font, cp, struct.unpack_from('<H', data, offset + 2)[0]))

render = (ROOT / 'Source/engine/render/text_render.cpp').read_text()
gmenu = (ROOT / 'Source/gmenu.cpp').read_text()
gamemenu = (ROOT / 'Source/gamemenu.cpp').read_text()
measure = function(render, 'int GetLineWidth(std::string_view text, GameFontTables size, int spacing, int *charactersInLine, CtrTextScale scale)')
text_width = function(gmenu, 'int GmenuGetTextWidth(TMenuItem *pItem, GameFontTables font, int spacing)')
font_picker = function(gmenu, 'GameFontTables GmenuGetFont(TMenuItem *pItem)')
spacing_picker = function(gmenu, 'int GmenuGetSpacing(TMenuItem *pItem)')
format_label = function(gamemenu, 'std::string GamemenuFormatModsLabel(std::string_view name, std::string_view value)')
prelude = r'''
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>
namespace devilution {
enum GameFontTables { GameFont12, GameFont24, GameFont30, GameFont42, GameFont46 };
enum class CtrTextScale { TopScreen, BottomScreen };
enum class text_color { ColorDialogWhite };
constexpr char32_t ZWSP=0x200b, Utf8DecodeError=0xfffd;
constexpr int gnScreenWidth=640;
std::unordered_map<unsigned,int> widths;
struct Glyph { int w; int width() const {return w;} };
struct CurrentFont {
 unsigned key;
 bool load(GameFontTables font,text_color,char32_t cp) {key=font*65536+cp;return widths.count(key);}
 Glyph glyph(uint8_t) {return {widths.at(key)};}
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
int ScaleNativeWidth(int w,CtrTextScale scale) {
 if(scale==CtrTextScale::TopScreen) return w>=0?(w*8+4)/5:-((-w*8)/5);
 return scale==CtrTextScale::BottomScreen?w*2:w;
}
[[noreturn]] void app_fatal(const char *) {std::abort();}
struct TMenuItem {const char *pszStr; bool localizeText; void *userData;};
std::string_view GmenuItemText(const TMenuItem *item) {return item->pszStr;}
int GetLineWidth(std::string_view,GameFontTables,int,int *,CtrTextScale);
'''
main = '\n} // namespace devilution\nint main(){ using namespace devilution; widths={' + ','.join('{' + str(font * 65536 + cp) + ',' + str(width) + '}' for font, cp, width in widths) + '};\n'
for name, value in cases:
    main += '{ const std::string label=GamemenuFormatModsLabel(' + json.dumps(name, ensure_ascii=False) + ',' + json.dumps(value, ensure_ascii=False) + '); '
    main += 'assert(label == std::string(' + json.dumps(name + ': ' + value, ensure_ascii=False) + ') || label == std::string(' + json.dumps(name, ensure_ascii=False) + ') + "\\n" + ' + json.dumps(value, ensure_ascii=False) + '); '
    main += 'assert(std::count(label.begin(),label.end(),\'\\n\')<=1); TMenuItem item{label.c_str(),false,reinterpret_cast<void *>(1)}; '
    main += 'const auto font=GmenuGetFont(&item); const auto spacing=GmenuGetSpacing(&item); '
    main += 'assert(font==GameFont12 && spacing<=1); const int width=GmenuGetTextWidth(&item,font,spacing); '
    main += 'if(width>gnScreenWidth-2*56-48){std::fprintf(stderr,"%s width %d font %d\\n",item.pszStr,width,font);return 1;} }\n'
main += '}\n'

with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'mods_menu.cpp'
    exe = Path(directory) / 'mods_menu'
    cpp.write_text(prelude + measure + format_label + text_width + spacing_picker + font_picker + main)
    subprocess.run(['c++', '-std=c++17', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# All live options are built once; navigation scrolls the visible row window.
assert 'GamemenuModsNextPage' not in gamemenu
assert 'GamemenuModsPreviousPage' not in gamemenu
assert 'for (OptionEntryBase *pOption : options)' in gamemenu
assert 'GMenuMaxVisibleRows = 5' in gmenu
assert 'y + GmenuVisibleRows() * GMenuItemHeight <= 220' in gmenu
live_filter = function(gamemenu, 'std::vector<OptionEntryBase *> GamemenuGetLiveModsOptions()')
assert 'GetCtrGameplayEntries()' in live_filter
toggle = function(gamemenu, 'void GamemenuModsOption(bool /*bActivate*/)')
assert 'ApplyCtrGameplayOptions();' in toggle
assert 'SaveOptions();' in toggle
assert 'gamemenu_back()' in gmenu
print(f'PASS: {len(cases)} production 3DS Mods labels fit beside the selectors; scrolling rows avoid the scrollbar and network address')
