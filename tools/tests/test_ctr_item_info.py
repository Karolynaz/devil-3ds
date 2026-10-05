#!/usr/bin/env python3
"""Exercise production ground labels and unique details with shipped EN/LT fonts."""
import ast
import csv
import json
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
def function(source, signature):
    start = source.index(signature); opening = source.index('\n{', start) + 1
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}'); end += 1
    return source[start:end]
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
names = sorted({r['name'] for f in ['assets/txtdata/items/unique_itemdat.tsv','mods/hf/txtdata/items/unique_itemdat.tsv']
                for r in csv.DictReader((ROOT/f).read_text().splitlines(),delimiter='\t')})
cases = names + [translations.get(n,n) for n in names] + ['Didžioji lazda su labai ilgu išverstu daikto pavadinimu ' * 3]
widths = []
for char in set(''.join(cases)+'?'):
    cp=ord(char); data=(ROOT/f'assets/fonts/12-{cp >> 8:02x}.clx').read_bytes()
    offset=struct.unpack_from('<I',data,4+4*(cp&255))[0]
    widths.append((cp,struct.unpack_from('<H',data,offset+2)[0]))
prelude = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "engine/rectangle.hpp"
#include "DiabloUI/ui_flags.hpp"
#include "utils/string_or_view.hpp"
using namespace devilution;
enum GameFontTables { GameFont12 };
enum class CtrTextScale { None, TopScreen, BottomScreen };
enum class text_color { ColorDialogWhite };
constexpr char32_t ZWSP=0x200b, Utf8DecodeError=0xfffd;
std::unordered_map<char32_t,int> widths;
struct Glyph { int w; int width() const { return w; } };
struct CurrentFont { char32_t cp;
 bool load(GameFontTables,text_color,char32_t value) {cp=value;return widths.count(cp);}
 Glyph glyph(uint8_t) { return {widths.at(cp)}; }
};
char32_t DecodeFirstUtf8CodePoint(std::string_view s, size_t *n) {
 uint8_t first=s[0];*n=first<128?1:first<224?2:first<240?3:4;
 char32_t cp=first&(*n==1?127:*n==2?31:*n==3?15:7);
 for(size_t i=1;i<*n;++i) cp=(cp<<6)|(uint8_t(s[i])&63);
 return cp;
}
struct Utf8CodePoints { std::u32string cps;
 explicit Utf8CodePoints(std::string_view s) {while(!s.empty()) {size_t n;cps.push_back(DecodeFirstUtf8CodePoint(s,&n));s.remove_prefix(n);}}
 auto begin() const{return cps.begin();} auto end() const{return cps.end();}
};
int ScaleSpacing(int w,CtrTextScale s){return s==CtrTextScale::TopScreen?(w*8+2)/5:s==CtrTextScale::BottomScreen?w*2:w;}
int ScaleCharWidth(int w,CtrTextScale s){return s==CtrTextScale::TopScreen?w*8/5:s==CtrTextScale::BottomScreen?w*2:w;}
[[noreturn]] void app_fatal(const char *) {std::abort();}
bool IsBreakableWhitespace(char32_t cp){return cp==' ';}
bool IsBreakAllowed(char32_t,char32_t){return false;}
std::vector<std::string_view> SplitByChar(std::string_view s,char ch){
 std::vector<std::string_view> result;
 for(;;){auto end=s.find(ch);result.push_back(s.substr(0,end));if(end==s.npos)break;s.remove_prefix(end+1);}return result;
}
int GetLineHeight(std::string_view,GameFontTables){return 12;}
constexpr int ICLASS_WEAPON=1,ICLASS_ARMOR=2,DUR_INDESTRUCTIBLE=255,IMISC_STAFF=3,ITEM_QUALITY_UNIQUE=2,IPL_INVALID=-1;
enum class ItemType { Gold, Other };
struct Item { int _iClass=0,_iMinDam=0,_iMaxDam=0,_iMaxDur=0,_iDurability=0,_iAC=0,_iMiscId=0,_iMaxCharges=0,_iCharges=0;
 int _iPrePower=-1,_iSufPower=-1,_iMagical=0,_iUid=0,_ivalue=0,_iCurs=0;
 ItemType _itype=ItemType::Other; std::string name;Point position;
 struct Animation {std::optional<std::array<int,1>> sprites=std::array<int,1>{0};int currentFrame=0;} AnimInfo;
 std::string_view getName() const {return name;} UiFlags getTextColor() const{return UiFlags::ColorWhitegold;}
};
std::array<Item,128> Items;
struct ItemLabel {int id,width,height;Point pos;StringOrView text;};
std::vector<ItemLabel> labelQueue;
std::array<std::optional<int>,1> labelCenterOffsets;
std::array<int,1> ItemCAnimTbl{0};
constexpr int BorderX=4,BorderY=2,MarginX=2,TILE_HEIGHT=32,PAL8_BLUE=16;
int LabelHeight(){return 15;} int TextMarginTop(){return -1;}
bool IsHighlightingLabelsEnabled(){return true;}
int GetScreenWidth(){return 640;} int GetViewportHeight(){return 240;}
std::pair<int,int> ClxMeasureSolidHorizontalBounds(int){return {0,32};}
struct Options {struct {bool noZoom=false; bool *zoom=&noZoom;} Graphics;} options;
Options &GetOptions(){return options;}
std::string _(const char *s){return s;}
std::string FormatInteger(int){return "gold";}
template<class... Args> std::string FormatRuntime(std::string s,Args...){return s;}
bool HeadlessMode=false,ShowUniqueItemInfoBox=false;
Item curruitem;
std::vector<std::string> bottomInfo,floatingInfo;
bool floatingEnabled=false;
void AddItemInfoBoxString(std::string s){(floatingEnabled?floatingInfo:bottomInfo).push_back(s);}
void AddInfoBoxString(std::string s){bottomInfo.push_back(s);}
std::string PrintItemPower(int type,const Item &){return "power "+std::to_string(type);}
void PrintItemInfo(const Item &){AddItemInfoBoxString("requirements");}
struct UniqueItem {std::array<struct Power,6> unused;};
'''.replace('struct UniqueItem {std::array<struct Power,6> unused;};', 'struct Power {int type=IPL_INVALID;}; struct UniqueItem {std::array<Power,6> powers;}; std::vector<UniqueItem> UniqueItems(1);')
prelude += r'''
int pcursitem=-1,PauseMode=0;bool isLabelHighlighted=false,MyPlayerIsDead=false;
Point cursPosition;
enum class PlayerActionType {None};PlayerActionType LastPlayerAction=PlayerActionType::None;
bool gmenu_is_active(){return false;} bool IsPlayerInStore(){return false;} bool IsMouseOverGameArea(){return true;}
Point mouse;Point GetWorldMousePosition(){return mouse;}
struct Surface {int width=640,height=240;int w() const{return width;}int h() const{return height;}
 Surface subregionY(int,int h) const{return {width,h};}
};
void FillRect(const Surface&,int,int,int,int,int){}
void DrawHalfTransparentRectTo(const Surface&,int,int,int,int){}
struct TextRenderOptions {UiFlags flags;};
int draws=0;
void DrawString(const Surface &out,std::string_view text,Rectangle rect,TextRenderOptions){
 ++draws;assert(rect.position.x>=0 && rect.position.x+rect.size.width<=out.w()+MarginX);
 for(auto line:SplitByChar(text,'\n')) assert(GetLineWidth(line,GameFont12,1,nullptr,CtrTextScale::TopScreen)<=rect.size.width-MarginX*2);
}
'''
render=(ROOT/'Source/engine/render/text_render.cpp').read_text()
labels=(ROOT/'Source/qol/itemlabels.cpp').read_text()
items=(ROOT/'Source/items.cpp').read_text()
measure=function(render,'int GetLineWidth(std::string_view text, GameFontTables size, int spacing, int *charactersInLine, CtrTextScale scale)')
# Measurement must precede the mock renderer that checks actual canvas widths.
insert=prelude.index('int draws=0;');prelude=prelude[:insert]+measure+'\n'+prelude[insert:]
test='int main(){widths={'+','.join('{'+str(cp)+','+str(w)+'}' for cp,w in widths)+'};\n'
for name in cases:
    test += '{ Items[0].name='+json.dumps(name,ensure_ascii=False)+';\n'
    test += r'''
    for(int x:{0,320,638}) {labelQueue.clear();draws=0;pcursitem=-1;AddItemToLabelQueue(0,{x,120});
      assert(labelQueue.size()==1);DrawItemNameLabels({});assert(draws==1 && labelQueue.empty());}
    }'''+'\n'
test += r'''
 // Dense ground piles must terminate and keep the focused item's full label.
 pcursitem=60;labelQueue.clear();draws=0;
 for(int i=0;i<100;++i){Items[i].name="The Butcher's Cleaver";AddItemToLabelQueue(i,{320,120});}
 DrawItemNameLabels({});assert(draws>0 && draws<=16 && labelQueue.empty());
 // Every unique power appears in the lower-screen description in either mode.
 Item item;item._iMagical=ITEM_QUALITY_UNIQUE;
 for(int i=0;i<6;++i)UniqueItems[0].powers[i].type=i;
 for(bool floating:{false,true}){
  floatingEnabled=floating;bottomInfo.clear();floatingInfo.clear();PrintItemDetails(item);
  for(int i=0;i<6;++i) assert(std::count(bottomInfo.begin(),bottomInfo.end(),"power "+std::to_string(i))==1);
  assert(!ShowUniqueItemInfoBox);
 }
 UniqueItems[0].powers[2].type=IPL_INVALID;bottomInfo.clear();PrintItemDetails(item);assert(bottomInfo.size()==2);
 item._iUid=-1;bottomInfo.clear();PrintItemDetails(item);assert(bottomInfo.empty());
}
'''
code=prelude+'\n'+function(render,'std::string WordWrapString(std::string_view text, unsigned width, GameFontTables size, int spacing, CtrTextScale scale)')+'\n'+function(labels,'void AddItemToLabelQueue(')+'\n'+function(labels,'void DrawItemNameLabels(')+'\n'+function(items,'void PrintItemDetails(')+'\n'+test
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory);(p/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20','-D__3DS__','-fsanitize=address,undefined','-I'+str(ROOT/'Source'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print(f'PASS: {len(cases)} Diablo/Hellfire EN/LT names, edge/crowded labels, six unique powers and invalid IDs (ASan/UBSan)')
