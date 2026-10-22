#!/usr/bin/env python3
"""Exercise native-world anchors on the separate 640-wide UI canvas."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
def function(file,signature):
 s=(ROOT/file).read_text();start=s.index(signature);opening=s.index('\n{',start)+1;end=opening+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
CPP=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "platform/ctr/ui_geometry.hpp"
using namespace devilution;
constexpr int TILE_WIDTH=64,TILE_HEIGHT=32,MarginX=2;
int GetScreenWidth(){return 640;}
struct Surface{};
struct Text {std::string value;Text(std::string s=""):value(s){}std::string_view str() const{return value;}};
struct ItemLabel{int id,width,height;Point pos;Text text;};
std::vector<ItemLabel> labelQueue;
enum class ItemType{Gold,Other};
struct Sprite{};
std::array<Sprite,1> sprites;
struct Item{ItemType _itype=ItemType::Other;int _ivalue=0,_iCurs=0;struct{std::array<Sprite,1>*sprites;int currentFrame;}AnimInfo{&::sprites,0};Text getName(){return Text("item");}};
std::array<Item,1> Items;
using StringOrView=Text;
std::array<std::optional<int>,1> labelCenterOffsets;
int ItemCAnimTbl[1]={0};
bool zoom=false;
struct Options{struct{bool*zoom;}Graphics{&zoom};} options;
Options&GetOptions(){return options;}
bool IsHighlightingLabelsEnabled(){return true;}
int LabelHeight(){return 12;}
enum GameFontTables{GameFont12};
enum class CtrTextScale{TopScreen};
const char*_(const char*s){return s;}
std::string FormatInteger(int){return "1";}
std::string FormatRuntime(const char*,std::string){return "gold";}
std::string WordWrapString(std::string_view s,int,GameFontTables,int,CtrTextScale){return std::string(s);}
std::vector<std::string_view> SplitByChar(std::string_view s,char){return {s};}
int GetLineWidth(std::string_view,GameFontTables,int,void*,CtrTextScale){return 28;}
int GetLineHeight(std::string_view,GameFontTables){return 12;}
std::pair<int,int> ClxMeasureSolidHorizontalBounds(Sprite){return {0,0};}
constexpr int GameFont30=30,GameFont24=24;
using UiFlags=int;
struct FloatingNumber{Point startPos;Displacement startOffset,endOffset;std::string text;uint32_t time,lastMerge;int style,id;bool reverseDirection;};
std::deque<FloatingNumber> FloatingQueue;
int GetGameFontSize(int){return 0;}
int GetLineWidth(const std::string&,int){return 32;}
uint32_t SDL_GetTicks(){return 0;}
void ClearExpiredNumbers(){}
struct Style{int flags;};
Rectangle drawn;
void DrawString(const Surface&,const std::string&,Rectangle rect,Style){drawn=rect;}
'''
TEST=r'''
int main(){
 for(bool useZoom:{false,true}){
  zoom=useZoom;labelQueue.clear();
  AddItemToLabelQueue(0,{useZoom?100:200,100});
  assert(labelQueue[0].pos.x+labelQueue[0].width/2==320);
  FloatingQueue={{{0,0},{0,0},{0,0},"1",2500,0,0,0,false}};
  DrawFloatingNumbers({}, {0,0}, {useZoom?68:168,100});
  assert(drawn.position.x+drawn.size.width/2==320);
 }
}
'''
with tempfile.TemporaryDirectory() as tmp:
 src=Path(tmp)/'test.cpp';exe=Path(tmp)/'test'
 src.write_text(CPP+function('Source/qol/itemlabels.cpp','void AddItemToLabelQueue(')+function('Source/qol/floatingnumbers.cpp','void DrawFloatingNumbers(')+TEST)
 subprocess.run(['c++','-std=c++20','-D__3DS__','-fsanitize=address,undefined','-I'+str(ROOT/'Source'),str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: item labels and floating numbers align with native world anchors, with and without zoom')
