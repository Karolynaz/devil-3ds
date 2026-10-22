#!/usr/bin/env python3
"""Execute production sale requests/confirmation: no lost items or gold."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'Source/qol/visual_store.cpp').read_text()
def fn(sig):
 a=s.index(sig);b=s.index('{',a);c=b+1;d=1
 while d:d+=(s[c]=='{')-(s[c]=='}');c+=1
 return s[a:c]
# Skip the forward declaration when extracting GetSellPrice.
a=s.index('int GetSellPrice(const Item &item)\n{')
price=s[a:s.index('\n}',a)+2]
code=r'''
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#define __3DS__ 1
#define check(v) do{if(!(v)){std::fprintf(stderr,"line %d\n",__LINE__);std::abort();}}while(0)
constexpr int ITEM_QUALITY_NORMAL=0,MaxGold=5000,CURSOR_HAND=0;
enum class VisualStoreVendor {Smith,Witch,Healer,Boy};
enum class HeroSpeech {ICantDoThat,IHaveNoRoom};enum class SfxID {ItemGold};
struct Point{};struct Size{int width,height;};
struct Item{int _iSeed=1,IDidx=1,_iCreateInfo=1,_ivalue=400,_iIvalue=800,_iMagical=0;bool _iIdentified=false;bool empty=false;Size size{1,1};bool isEmpty()const{return empty;}void clear(){empty=true;}};
int room=0,received=0,sounds=0,redraw=0,spoken=0,removed=0,VisualStoreButtonPressed=-1;
struct Player{Item InvList[4],HoldItem;int _pNumInv=2,_pGold=100;void SaySpecific(HeroSpeech){spoken++;}void RemoveInvItem(int index){room+=InvList[index].size.width*InvList[index].size.height*MaxGold;for(int i=index;i+1<_pNumInv;i++)InvList[i]=InvList[i+1];--_pNumInv;removed++;}}player;
Player *MyPlayer=&player;
bool IsVisualStoreOpen=true;struct{VisualStoreVendor vendor=VisualStoreVendor::Smith;}VisualStore;
struct CtrPendingSale {bool active=false;int index=-1;Item item;VisualStoreVendor vendor;int price=0;}PendingSale;
bool VendorAcceptsSale(){return VisualStore.vendor==VisualStoreVendor::Smith||VisualStore.vendor==VisualStoreVendor::Witch;}
bool CanSellToCurrentVendor(const Item&i){return VendorAcceptsSale()&&!i.isEmpty()&&i.IDidx!=99;}
void RedrawEverything(){redraw++;}void NewCursor(int){}
void PlaySFX(SfxID){sounds++;}Size GetInventorySize(const Item&i){return i.size;}
int RoomForGold(){return room;}
int AddGoldToInventory(Player&,int amount){check(amount<=room);room-=amount;received+=amount;return 0;}
'''+price+fn('bool SameSaleItem(')+fn('void SellItemToVisualStore(')+fn('void CheckVisualStorePaste(')+fn('bool Is3DSVisualStoreSalePending(')+fn('void Confirm3DSVisualStoreSale(')+r'''
void reset(){player=Player{};room=received=sounds=spoken=removed=0;PendingSale.active=false;VisualStore.vendor=VisualStoreVendor::Smith;IsVisualStoreOpen=true;}
int main(){
 reset();VisualStoreButtonPressed=2;Confirm3DSVisualStoreSale(false);check(VisualStoreButtonPressed==-1);
 SellItemToVisualStore(0);check(Is3DSVisualStoreSalePending()&&received==0&&removed==0);Confirm3DSVisualStoreSale(false);check(received==0&&removed==0);
 SellItemToVisualStore(0);Confirm3DSVisualStoreSale(true);check(received==100&&player._pGold==200&&removed==1);Confirm3DSVisualStoreSale(true);check(received==100&&removed==1);
 reset();player.InvList[0]._ivalue=24000;SellItemToVisualStore(0);Confirm3DSVisualStoreSale(true);check(received==0&&removed==0&&spoken==1);
 reset();SellItemToVisualStore(0);player.InvList[0]._iSeed++;Confirm3DSVisualStoreSale(true);check(received==0&&removed==0);
 reset();SellItemToVisualStore(0);player.InvList[0]._ivalue++;player.InvList[0]._ivalue+=4;Confirm3DSVisualStoreSale(true);check(received==0);
 reset();SellItemToVisualStore(0);VisualStore.vendor=VisualStoreVendor::Witch;Confirm3DSVisualStoreSale(true);check(received==0);
 reset();SellItemToVisualStore(-1);SellItemToVisualStore(4);check(!PendingSale.active);
 reset();CheckVisualStorePaste({});Confirm3DSVisualStoreSale(true);check(!player.HoldItem.isEmpty()&&received==0&&spoken==1);
 reset();room=100;CheckVisualStorePaste({});Confirm3DSVisualStoreSale(true);check(player.HoldItem.isEmpty()&&received==100&&player._pGold==200);
 reset();player.InvList[0].IDidx=99;SellItemToVisualStore(0);check(!PendingSale.active&&received==0&&spoken==1);
 reset();player.InvList[0]._iMagical=1;player.InvList[0]._iIdentified=true;SellItemToVisualStore(0);check(PendingSale.price==200);Confirm3DSVisualStoreSale(true);check(received==200&&player._pGold==300);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'t.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
print('PASS: production vendor sale confirmation, cancel/replay, changed item/vendor, full inventory and exact gold accounting (ASan/UBSan)')
