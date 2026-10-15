#!/usr/bin/env python3
"""Check the patched production RNG batches PS IPC and handles failure safely."""
from pathlib import Path
import subprocess,tempfile,sys
source=(Path(sys.argv[1])/'ext/ZeroTierOne/node/Utils.cpp').read_text()
start=source.index('void Utils::getSecureRandom(');op=source.index('\n{',start)+1;end=op+1;depth=1
while depth:
 depth+=(source[end]=='{')-(source[end]=='}');end+=1
cpp=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
using Result=int;
using uint8_t=std::uint8_t;using uint64_t=std::uint64_t;
#define R_SUCCEEDED(x) ((x)>=0)
#define R_FAILED(x) ((x)<0)
int calls=0;bool fail=false;
int psGetSessionHandle(){return 1;}int psInit(){return 0;}
int PS_GenerateRandomBytes(void*buf,unsigned size){++calls;assert(size==512);if(fail)return -1;std::memset(buf,0x80+(calls%32),size);return 0;}
void ctr_zt_trace(unsigned,int){}
struct Mutex{std::mutex mutex;struct Lock{std::lock_guard<std::mutex>lock;Lock(Mutex&m):lock(m.mutex){}};};
struct Salsa20{void init(void*,void*){}};
struct Utils{static void getSecureRandom(void*,unsigned);};
'''
cpp+=source[start:end]
cpp+=r'''
int main(){
 uint8_t out[1024];
 for(int i=0;i<128;++i){Utils::getSecureRandom(out+i*8,8);}
 assert(calls==2);for(auto c:out)assert(c>=0x80);
 fail=true;bool caught=false;try{Utils::getSecureRandom(out,1);}catch(const std::runtime_error&){caught=true;}assert(caught);
 fail=false;Utils::getSecureRandom(out,1);assert(out[0]>=0x80);
 std::vector<std::thread>workers;
 for(int i=0;i<4;++i)workers.emplace_back([](){uint8_t b[8];for(int n=0;n<100;++n){Utils::getSecureRandom(b,8);for(auto c:b)assert(c>=0x80);}});
 for(auto&t:workers)t.join();
 assert(calls<12);
}
'''
with tempfile.TemporaryDirectory() as t:
 src=Path(t)/'test.cpp';exe=Path(t)/'test';src.write_text(cpp)
 subprocess.run(['c++','-std=c++17','-D__3DS__','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: 128 small random requests use two PS calls; failures and concurrent readers are safe')
