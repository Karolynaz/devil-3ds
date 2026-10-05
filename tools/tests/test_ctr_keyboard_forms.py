#!/usr/bin/env python3
"""Exercise production native form queuing, submit/cancel and UTF-8 byte limits."""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'Source/platform/ctr/keyboard.cpp').read_text()
body=source[source.index('constexpr size_t MAX_TEXT_LENGTH'):source.index('std::optional<int> ctr_vkbdNumberInput')]
prefix=r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
std::string_view _(const char *s) { return s; }
namespace devilution { void CopyUtf8(char *p, std::string_view s, size_t n) { s=s.substr(0,n-1); std::memcpy(p,s.data(),s.size()); p[s.size()]=0; } }
enum { SWKBD_TYPE_WESTERN, SWKBD_ANYTHING, SWKBD_NOTEMPTY_NOTBLANK };
enum SwkbdCallbackResult { SWKBD_CALLBACK_OK, SWKBD_CALLBACK_CONTINUE };
enum SwkbdButton { SWKBD_BUTTON_CANCEL, SWKBD_BUTTON_CONFIRM };
struct SwkbdState { int maxBytes=0,validation=0; void *user=nullptr; SwkbdCallbackResult (*filter)(void *,const char **,const char *,size_t)=nullptr; };
int opens=0; bool confirm=true, expectEmpty=false; std::string input="12345";
void swkbdInit(SwkbdState *s,int,int,int maximum) { s->maxBytes=maximum; }
void swkbdSetValidation(SwkbdState *s,int validation,int,int) { s->validation=validation; }
void swkbdSetFilterCallback(SwkbdState *s,SwkbdCallbackResult (*filter)(void *,const char **,const char *,size_t),void *user) { s->filter=filter; s->user=user; }
void swkbdSetInitialText(SwkbdState *,const char *) {}
void swkbdSetHintText(SwkbdState *,const char *) {}
SwkbdButton swkbdInputText(SwkbdState *s,char *out,size_t size) {
 ++opens; assert(s->maxBytes==15);
 assert(s->validation==(expectEmpty?SWKBD_ANYTHING:SWKBD_NOTEMPTY_NOTBLANK));
 const char *message=nullptr;
 assert(s->filter(s->user,&message,"ąąąąąąąą",16)==SWKBD_CALLBACK_CONTINUE && message);
 assert(s->filter(s->user,&message,"Cathan",6)==SWKBD_CALLBACK_OK);
 assert(input.size()<size); std::strcpy(out,input.c_str());
 return confirm?SWKBD_BUTTON_CONFIRM:SWKBD_BUTTON_CANCEL;
}
'''
suffix=r'''
int submits=0,cancels=0; std::string received;
void cancelled() { ++cancels; }
void submitted(std::string_view text) {
 ++submits; received=text;
 if (submits==1) ctr_vkbdInput("Next form","",submitted,cancelled,15,true);
}
int main() {
 ctr_vkbdInput("Password","",submitted,cancelled,15,false);
 ctr_vkbdFlush(); assert(opens==1 && submits==1 && received=="12345" && eventCount==1);
 expectEmpty=true; input=""; ctr_vkbdFlush(); assert(opens==2 && submits==2 && received.empty());
 ctr_vkbdInput("Name","",submitted,cancelled,15,false);
 expectEmpty=false; confirm=false; ctr_vkbdFlush(); assert(cancels==1 && submits==2);
 ctr_vkbdInput("Old menu","",submitted,cancelled,15,false); ctr_vkbdClear();
 ctr_vkbdFlush(); assert(opens==3);
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory); (p/'test.cpp').write_text(prefix+body+suffix)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: native form submit/cancel, empty join password, UTF-8 byte limit and next-menu queue (ASan/UBSan)')
