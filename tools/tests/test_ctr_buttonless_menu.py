#!/usr/bin/env python3
"""Exercise production input paths for status screens without a Back row."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'Source/DiabloUI/diabloui.cpp').read_text()
def function(signature):
 a=s.index(signature);b=s.index('\n{',a)+1;c=b+1;depth=1
 while depth:
  depth+=(s[c]=='{')-(s[c]=='}');c+=1
 return s[a:c]
code=r'''
#include <cassert>
#include <cstddef>
#include <optional>
#define __SWITCH__ 1
using Flags=int;
namespace UiFlags {constexpr int ElementHidden=1,ElementDisabled=2,NeedsNextElement=4;}
bool HasAnyOf(Flags a,Flags b){return (a&b)!=0;}
struct Item{int uiFlags=0,m_text=0;};
struct List{Item item[2];int reads=0;Item*GetItem(size_t i){assert(i<2);reads++;return &item[i];}};
List *gUiList=nullptr;
size_t SelectedItem=0,SelectedItemMax=0;
bool UiItemsWraps=false;
void AdjustListOffset(size_t){}
void SpeakText(int){}
int selectSounds=0,moveSounds=0,selected=0,back=0;
void UiPlayMoveSound(){moveSounds++;}
void UiPlaySelectSound(){selectSounds++;}
void (*gfnListFocus)(size_t)=nullptr;
void (*gfnListSelect)(size_t)=nullptr;
void (*gfnListEsc)()=nullptr;
bool allowEmptyTextInput=false;
struct Input{bool empty()const{return true;}};
std::optional<Input>UiTextInputState;
'''
code+='\n'.join(function(sig) for sig in ['void UiFocus(std::size_t itemIndex, bool checkUp, bool ignoreItemsWraps = false)','void UiFocusUp()','void UiFocusDown()','void UiFocusNavigationSelect()','void UiFocusNavigationEsc()'])
code+=r'''
int main(){
 gfnListEsc=[](){back++;};
 for(bool wrap:{false,true}){
  UiItemsWraps=wrap;
  UiFocusUp();UiFocusDown();UiFocus(1,false);
  UiFocusNavigationSelect();assert(selected==0&&SelectedItem==0);
  UiFocusNavigationEsc();
 }
 assert(back==2);
 List list;gUiList=&list;SelectedItemMax=1;UiItemsWraps=false;
 gfnListSelect=[](size_t i){selected=int(i)+1;};
 UiFocusDown();assert(SelectedItem==1&&list.reads>0);
 UiFocusNavigationSelect();assert(selected==2);
 UiFocusUp();assert(SelectedItem==0);
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'menu.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined',str(p/'menu.cpp'),'-o',str(p/'menu')],check=True)
 subprocess.run([str(p/'menu')],check=True)
print('PASS: buttonless status screens safely ignore navigation/A, B returns, normal list selection preserved (ASan/UBSan)')
