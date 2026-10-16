#!/usr/bin/env python3
"""Check production menu scrolling and native skull pixel sampling."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
source = (ROOT/'Source/gmenu.cpp').read_text()
def function(signature):
    start=source.index(signature);opening=source.index('\n{',start)+1;end=opening+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
prelude=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>
#include "engine/point.hpp"
#include "platform/ctr/pixel_geometry.hpp"
using namespace devilution;
constexpr int GMenuMaxVisibleRows=5;
int sgFirstVisibleMenuItem=0,sgCurrentMenuIdx=0;
bool isDraggingSlider=false;
struct TMenuItem{void (*fnMenu)(bool)=nullptr;bool active=true;bool enabled()const{return active;}};
TMenuItem *sgpCurrItem=nullptr,*sgpCurrentMenu=nullptr;
enum class SfxID{MenuMove};void PlaySFX(SfxID){}
struct ClxSprite{
 int width()const{return 27;}int height()const{return 29;}
 uint8_t pixel(int x,int y)const{
  if((x==0||x==26)&&(y==0||y==28))return 1; // transparent corners
  if(x==10&&y==15)return 0; // opaque black eye must survive
  return 3+(x+3*y)%230;
 }
};
struct Surface {
 struct {int x=0,y=0;} region;
 int width,height;mutable std::vector<uint8_t> pixels;Surface *surface=this;
 Surface(int w,int h):width(w),height(h),pixels(w*h,2){}
 int w()const{return width;}int h()const{return height;}
 bool InBounds(Point p)const{return p.x>=0&&p.x<width&&p.y>=0&&p.y<height;}
 void SetPixel(Point p,uint8_t c)const{assert(InBounds(p));pixels[p.y*width+p.x]=c;}
 uint8_t operator[](Point p)const{assert(InBounds(p));return pixels[p.y*width+p.x];}
};
using OwnedSurface=Surface;
void SDL_FillSurfaceRect(Surface *s,void*,int color){std::fill(s->pixels.begin(),s->pixels.end(),color);}
void RenderClxSprite(const Surface &out,ClxSprite s,Point pos){
 for(int y=0;y<s.height();y++)for(int x=0;x<s.width();x++)out.SetPixel({pos.x+x,pos.y+y},s.pixel(x,y));
}
void handler(bool){}
'''
code=prelude+'\n'.join(function(s) for s in ('int GmenuVisibleRows(', 'int GmenuFirstVisibleRow(', 'void GmenuScrollToCurrent(', 'void GmenuUpDown(', 'void DrawGMenuSliderMarker('))+r'''
int main(){
 for(int count=1;count<=40;count++){
  std::vector<TMenuItem> items(count+1);for(int i=0;i<count;i++)items[i].fnMenu=handler;
  for(int i=3;i<count;i+=9)items[i].active=false;
  sgpCurrentMenu=items.data();sgpCurrItem=items.data();sgCurrentMenuIdx=count;sgFirstVisibleMenuItem=0;
  for(bool down:{true,false})for(int n=0;n<count*3;n++){
   auto previous=sgpCurrItem-items.data();GmenuUpDown(down);
   int selected=sgpCurrItem-items.data();assert(selected>=0&&selected<count&&sgpCurrItem->enabled());
   assert(selected>=sgFirstVisibleMenuItem&&selected<sgFirstVisibleMenuItem+GmenuVisibleRows());
   assert(sgFirstVisibleMenuItem+GmenuVisibleRows()<=count);
   if(down&&previous==count-1)assert(selected==0&&sgFirstVisibleMenuItem==0);
   if(!down&&previous==0)assert(sgFirstVisibleMenuItem==std::max(0,count-5));
  }
 }
 // Every source skull column/row appears once after the real 640 -> 400
 // presenter sampling. Include all phases, subregion offsets and black ink.
 ClxSprite skull;
 for(int region:{0,3,11})for(int phase=0;phase<8;phase++){
  Surface out(640-region,80);out.region.x=region;Point position{100+phase,20};
  DrawGMenuSliderMarker(out,skull,position);
  int origin=CtrInverseColumn(region+position.x,400);
  for(int y=0;y<29;y++)for(int x=0;x<27;x++){
   int canvasX=CtrNativeColumn(origin+x,400)-region;
   auto expected=skull.pixel(x,y);if(expected==1)expected=2;
   assert(out[Point(canvasX,20+y)]==expected);
  }
  assert(out[Point(CtrNativeColumn(origin+27,400)-region,20)]==2);
 }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'menu.cpp').write_text(code)
    subprocess.run(['c++','-std=c++17','-D__3DS__','-fsanitize=address,undefined','-I'+str(ROOT/'Source'),str(p/'menu.cpp'),'-o',str(p/'menu')],check=True)
    subprocess.run([str(p/'menu')],check=True)
assert source.count('DrawDiabloMsgBackground(out, { { 0, 0 }, { out.w(), GMenuTopScreenHeight } });')==2
assert 'const int i = visibleRow + sgFirstVisibleMenuItem;' in source
assert 'TMenuItem *i = sgpCurrentMenu + sgFirstVisibleMenuItem;' in source
assert 'LoadCel("data\\\\option", SliderMarkerSpriteWidth)' in source
print('PASS: scrolling through 1..40 rows, disabled entries, both wrap directions, mouse row offset, full-screen backdrop, exact 27x29 skull pixels and transparency (ASan/UBSan)')
