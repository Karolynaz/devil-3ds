#!/usr/bin/env python3
"""Exercise actual 3DS service setup/cleanup with failure injection; no emulator."""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SDK = r'''
#pragma once
#include <cstdint>
#include <cstddef>
using u32=uint32_t; using s64=int64_t; using Result=int32_t;
using LightLock=int;
#define R_FAILED(r) ((r)<0)
constexpr int NDM_EXCLUSIVE_STATE_INFRASTRUCTURE=1;
void LightLock_Lock(LightLock *); void LightLock_Unlock(LightLock *);
Result acInit(); void acExit(); Result ndmuInit(); void ndmuExit();
Result NDMU_EnterExclusiveState(int); Result NDMU_LeaveExclusiveState();
Result ACU_GetWifiStatus(u32 *); void svcSleepThread(s64);
Result socInit(u32 *,size_t); void socExit();
u32 psGetSessionHandle(); Result psInit(); Result PS_GenerateRandomBytes(void *,size_t);
'''
HARNESS = r'''
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>
#include <3ds.h>
int failure=0, wifiWait=0, locked=0;
bool wifi=true,ac=false,ndm=false,reserved=false,soc=false,ps=false;
bool allocationWorks=true;
int acStarts=0, socStarts=0;
std::vector<std::string> order;
void LightLock_Lock(LightLock *) { assert(!locked);locked=1; }
void LightLock_Unlock(LightLock *) { assert(locked);locked=0; }
Result acInit() { assert(!ac);if(failure==1)return -1;ac=true;++acStarts;return 0; }
void acExit() { assert(ac && !ndm && !soc);ac=false;order.push_back("ac"); }
Result ndmuInit() { assert(ac && !ndm);if(failure==2)return -2;ndm=true;return 0; }
void ndmuExit() { assert(ndm && !reserved && !soc);ndm=false;order.push_back("ndm"); }
Result NDMU_EnterExclusiveState(int state) { assert(state==1 && ndm && !reserved);if(failure==3)return -3;reserved=true;return 0; }
Result NDMU_LeaveExclusiveState() { assert(reserved && !soc);reserved=false;order.push_back("leave");return 0; }
Result ACU_GetWifiStatus(u32 *out) { assert(ac && reserved);if(failure==4)return -4;*out=wifi?2:0;return 0; }
void svcSleepThread(s64) { ++wifiWait; }
Result socInit(u32 *buffer,size_t size) { assert(buffer && size==1048576 && reserved && !soc);if(failure==5)return -5;soc=true;++socStarts;return 0; }
void socExit() { assert(soc);soc=false;order.push_back("soc"); }
u32 psGetSessionHandle() { return ps?1:0; }
Result psInit() { assert(!ps);if(failure==6)return -6;ps=true;return 0; }
Result PS_GenerateRandomBytes(void *,size_t size) { assert(ps && size==32);return failure==7?-7:0; }
extern "C" void *test_memalign(size_t,size_t size) { return allocationWorks?std::malloc(size):nullptr; }
#define memalign test_memalign
#include "platform/ctr/sockets.cpp"
#undef memalign
using namespace devilution;
int main() {
    assert(!ac && !soc && !ndm); // Lazy: no startup networking in single player.
    for(int stage=1;stage<=5;++stage) {
        failure=stage;assert(!n3ds_socInit());
        assert(!ac && !ndm && !reserved && !soc && !socBuffer);
        assert(n3ds_networkError().find("failed (0x")!=std::string::npos);
        n3ds_socExit();
    }
    failure=0;allocationWorks=false;assert(!n3ds_socInit());
    assert(n3ds_networkError()=="Not enough memory for networking.");
    assert(!ac && !ndm && !soc);allocationWorks=true;
    wifi=false;wifiWait=0;assert(!n3ds_socInit() && wifiWait==50);
    assert(n3ds_networkError().starts_with("No Wi-Fi") && !ac && !ndm && !soc);
    wifi=true;assert(n3ds_socInit());int starts=socStarts,acCount=acStarts;
    assert(n3ds_socInit() && socStarts==starts && acStarts==acCount);
    wifi=false;assert(!n3ds_socInit() && soc); // No deleting sockets in use by ZeroTier.
    wifi=true;assert(n3ds_socInit() && socStarts==starts && n3ds_networkError().empty());
    failure=6;assert(!n3ds_initSecureRandom() && !ps);
    assert(n3ds_networkError().starts_with("ps:ps initialization failed"));
    failure=0;assert(n3ds_initSecureRandom() && n3ds_initSecureRandom());
    failure=7;assert(!n3ds_initSecureRandom());
    assert(n3ds_networkError().starts_with("ps:ps random generation failed"));
    order.clear();n3ds_socExit();
    assert((order==std::vector<std::string>{"soc","leave","ndm","ac"}));
    order.clear();n3ds_socExit();assert(order.empty());
    failure=0;assert(n3ds_socInit());n3ds_socExit();
}
'''
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    (temp/'utils').mkdir()
    (temp/'utils/log.hpp').write_text('#pragma once\nnamespace devilution { template<class... T> void LogError(T...) {} }\n')
    (temp/'malloc.h').write_text('#pragma once\n#include <cstdlib>\n')
    (temp/'3ds.h').write_text(SDK)
    (temp/'test.cpp').write_text(HARNESS)
    exe = temp/'test'
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', f'-I{temp}', f'-I{ROOT/"Source"}',
                    str(temp/'test.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

spec = importlib.util.spec_from_file_location('packages', ROOT/'tools/verify_3ds_packages.py')
packages = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packages)
cia = ROOT/'3DS_Release/3.03_test_23a9f8d/devil-3ds.cia'
if cia.exists():
    data = cia.read_bytes()
    try:
        packages.verify_network_services(data)
        raise AssertionError('Old release must expose the missing ps:ps permission')
    except ValueError as error:
        assert 'ps:ps' in str(error)
    assert data.count(b'\0' * 8) >= 2
    # Populate an unused slot in both authentic service tables.
    import struct
    h, _, _, c, t, m = struct.unpack_from('<IHHIII', data)
    align = lambda n: (n + 63) & ~63
    exheader = align(h)+align(c)+align(t)+align(m)+0x200
    changed = bytearray(data)
    for offset in (0x250, 0x650):
        position = exheader+offset+27*8
        assert changed[position:position+8] == b'\0'*8
        changed[position:position+8] = b'ps:ps\0\0\0'
    packages.verify_network_services(changed)
    changed[exheader+0x650+27*8:exheader+0x650+28*8] = b'\0'*8
    try:
        packages.verify_network_services(changed)
        raise AssertionError('Requested permission without allowed permission must fail')
    except ValueError as error:
        assert 'allowed' in str(error) and 'ps:ps' in str(error)
print('PASS: 3DS service failures, RNG preflight, Wi-Fi retries, teardown and CIA permissions (ASan/UBSan)')
