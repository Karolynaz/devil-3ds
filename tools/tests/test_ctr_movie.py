#!/usr/bin/env python3
"""Check production movie pixels against the previous two-stage mapping."""
from pathlib import Path
import tempfile, subprocess
s=Path('Source/storm/storm_svid.cpp').read_text()
f=s[s.index('void BlitCtrMoviePixels('):s.index('\n#endif\n\nbool BlitFrame()',s.index('void BlitCtrMoviePixels('))]
code=r'''
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cassert>
struct Size {int width,height;};
struct SDL_Surface {int w,h,pitch;void *pixels;};
'''+f+r'''
int main() {
 for(Size size: {Size{320,200},Size{640,480},Size{640,200},Size{400,240}}) {
  int w=size.width,h=size.height; Size fit=w*240>h*400?Size{400,h*400/w}:Size{w*240/h,240};
  std::vector<uint8_t> src((w+7)*h),dst(656*480,77);
  for(int y=0;y<h;++y) for(int x=0;x<w;++x) src[y*(w+7)+x]=(x*13+y*17)%256;
  SDL_Surface source{w,h,w+7,src.data()},output{640,480,656,dst.data()};
  BlitCtrMoviePixels(&source,&output,fit);
  int left=(400-fit.width)/2,top=(240-fit.height)/2;
  for(int y=0;y<240;++y) for(int x=0;x<640;++x) {
   int nx=((x+1)*400+639)/640-1;
   uint8_t expected=0;
   if(nx>=left && nx<left+fit.width && y>=top && y<top+fit.height)
    expected=src[((y-top)*h/fit.height)*(w+7)+(nx-left)*w/fit.width];
   assert(dst[y*656+x]==expected);
  }
  // Don't touch pitch padding or the already cleared lower screen.
  for(int y=0;y<240;++y) for(int x=640;x<656;++x) assert(dst[y*656+x]==77);
  for(size_t i=656*240;i<dst.size();++i) assert(dst[i]==77);
 }
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: movie pixel mapping, letterboxing and padded surfaces (ASan/UBSan)')
