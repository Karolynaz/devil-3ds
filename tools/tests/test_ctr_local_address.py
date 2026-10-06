#!/usr/bin/env python3
"""Check native address byte order and service failures without an emulator."""
from pathlib import Path
import tempfile, subprocess
s=Path('Source/platform/ctr/sockets.cpp').read_text()
f=s[s.index('std::string n3ds_localAddress()'):s.index('std::string n3ds_networkError()')]
code=r'''
#include <arpa/inet.h>
#include <cassert>
#include <string>
struct SocketGuard {};
bool initialized=false; int result=0; in_addr fake{};
int SOCU_GetIPInfo(in_addr *ip,in_addr *mask,in_addr *broadcast) {
 assert(mask && broadcast); *ip=fake; return result;
}
'''+f+r'''
int main() {
 assert(n3ds_localAddress().empty());
 initialized=true;
 assert(n3ds_localAddress().empty());
 assert(inet_pton(AF_INET,"192.168.1.123",&fake)==1);
 assert(n3ds_localAddress()=="192.168.1.123");
 result=-1; assert(n3ds_localAddress().empty());
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: local address byte order, disconnected state and SOC failure')
