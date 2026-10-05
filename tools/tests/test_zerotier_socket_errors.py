#!/usr/bin/env python3
"""Exercise the production virtual socket setup and discovery error paths."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'Source/dvlnet/protocol_zt.cpp').read_text()


def function(signature):
    start = source.index(signature)
    opening = source.index('\n{', start) + 1
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


prelude = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <expected>
#include <string>
#include <string_view>
#include <vector>
using buffer_t=std::vector<unsigned char>;
struct PacketError { std::string message; };
template<class... A> PacketError ProtocolError(std::string_view message,A&&...) { return {std::string(message)}; }
struct sockaddr {};
struct Address { unsigned char s6_addr[16] {}; } in6addr_any;
struct ZeroTierSocketAddress { unsigned sin6_port, sin6_family; Address sin6_addr; };
constexpr unsigned AF_INET6=10, SOCK_DGRAM=2, SOCK_STREAM=1;
#undef htons
unsigned htons(unsigned value) { return value; }
bool ready=false;
bool zerotier_network_ready() {return ready;}
int failure=0, sockets=0, binds=0, closes=0, sends=0, sentBytes=0;
int lwip_socket(unsigned,unsigned,unsigned) {++sockets;if(failure==sockets) return -1; return sockets;}
int lwip_bind(int fd,sockaddr*,unsigned) { assert(fd>=0); ++binds; return failure==binds+2?-1:0; }
int lwip_listen(int fd,int) { assert(fd>=0); return failure==5?-1:0; }
int lwip_close(int fd) {assert(fd>=0);++closes;return 0;}
int lwip_sendto(int fd,const void*,size_t,int,const sockaddr*,unsigned) {assert(fd>=0);++sends;return sentBytes;}
class protocol_zt {
public:
 struct endpoint {std::array<unsigned char,16> addr{};};
 int fd_udp=-1,fd_tcp=-1;unsigned default_port=6112;
 void close_all() {if(fd_udp!=-1) lwip_close(fd_udp);if(fd_tcp!=-1) lwip_close(fd_tcp);fd_udp=fd_tcp=-1;}
 static void set_reuseaddr(int fd) {assert(fd>=0);}
 static void set_nonblock(int fd) {assert(fd>=0);}
 static void set_nodelay(int fd) {assert(fd>=0);}
 std::expected<bool,PacketError> network_online();
 bool send_oob(const endpoint &,const buffer_t &) const;
};
'''
test = r'''
int main() {
 protocol_zt protocol;
 auto status=protocol.network_online();assert(status && !*status && sockets==0);
 assert(!protocol.send_oob({}, {1,2,3}) && sends==0);
 ready=true;
 for(int fail=1;fail<=5;++fail) {
  failure=fail;sockets=binds=closes=0;
  status=protocol.network_online();assert(!status);
  assert(protocol.fd_tcp==-1 && protocol.fd_udp==-1);
  failure=0;sockets=binds=0;
  status=protocol.network_online();assert(status && *status); // Retry succeeds.
  protocol.close_all();
 }
 failure=0;sockets=binds=0;
 status=protocol.network_online();assert(status && *status);
 status=protocol.network_online();assert(status && *status && sockets==2); // Reuse.
 for(int result: {-1,0,2,3}) {
  sentBytes=result;assert(protocol.send_oob({}, {1,2,3})==(result==3));
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'sockets.cpp'; exe = Path(directory) / 'sockets'
    cpp.write_text(prelude + function('std::expected<bool, PacketError> protocol_zt::network_online(') + function('bool protocol_zt::send_oob(') + test)
    subprocess.run(['c++', '-std=c++23', '-fsanitize=address,undefined', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: socket creation/bind/listen failures clean up, retry and report failed discovery (ASan/UBSan)')
