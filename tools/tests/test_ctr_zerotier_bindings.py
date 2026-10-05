#!/usr/bin/env python3
"""Run the patched pinned Binder::refresh with mock sockets, without an emulator.
Usage: test_ctr_zerotier_bindings.py PATH_TO_PATCHED_LIBZT
"""
from pathlib import Path
import os, subprocess, sys, tempfile
p=Path(sys.argv[1])/'ext/ZeroTierOne/osdep/Binder.hpp';source=p.read_text()
method=source[source.index('template <typename PHY_HANDLER_TYPE, typename INTERFACE_CHECKER> void refresh'):source.index('\n\t/**\n\t * @return All currently bound')]
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#define __3DS__ 1
#define ZT_UDP_DESIRED_BUF_SIZE 1048576
#define ZT_BINDER_MAX_BINDINGS 16
struct Mutex { struct Lock { Lock(Mutex &) {} }; };
struct InetAddress : sockaddr_storage {
 unsigned port;
 InetAddress():sockaddr_storage{},port(0) {}
 InetAddress(uint32_t,unsigned p):InetAddress() { ss_family=AF_INET;port=p; }
 InetAddress(const void *,int,unsigned p):InetAddress() { ss_family=AF_INET6;port=p; }
 void setPort(unsigned p) { port=p; }
 bool operator<(const InetAddress &b)const { return ss_family==b.ss_family?port<b.port:ss_family<b.ss_family; }
 bool operator==(const InetAddress &b)const { return ss_family==b.ss_family && port==b.port; }
};
struct PhySocket {};
template<class H>struct Phy {
 int udpCalls=0,tcpCalls=0,closed=0;bool failUdp=false;PhySocket udp,tcp;
 PhySocket *udpBind(const sockaddr *address,void *,int) { assert(address->sa_family==AF_INET); ++udpCalls; return failUdp?nullptr:&udp; }
 PhySocket *tcpListen(const sockaddr *address,void *) { assert(address->sa_family==AF_INET); ++tcpCalls;return &tcp; }
 void setIfName(PhySocket *,char *,int) {}
 void close(PhySocket *s,bool) { if(s) ++closed; }
};
struct Checker {};
struct Binder {
 struct Binding { InetAddress address; PhySocket *udpSock=nullptr,*tcpListenSock=nullptr; } _bindings[16];
 unsigned _bindingCount=0; Mutex _lock;
'''
suffix=r'''
};
int main() {
 Binder binder;Phy<int> phy;Checker checker;unsigned ports[]={9993,12345};
 binder.refresh(phy,ports,2,{},checker);
 assert(binder._bindingCount==2 && phy.udpCalls==2 && phy.tcpCalls==2);
 binder.refresh(phy,ports,2,{},checker);assert(phy.udpCalls==2 && binder._bindingCount==2);
 binder.refresh(phy,ports,1,{},checker);assert(binder._bindingCount==1 && phy.closed==2);
 Binder failed;Phy<int> bad;bad.failUdp=true;failed.refresh(bad,ports,1,{},checker);
 assert(failed._bindingCount==0 && bad.closed==1);
}
'''
with tempfile.TemporaryDirectory() as directory:
 d=Path(directory);(d/'test.cpp').write_text(prefix+method+suffix)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined',str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
print('PASS: pinned ZeroTier creates IPv4 bindings without interface enumeration, reuses sockets and handles bind failure (ASan/UBSan)')
