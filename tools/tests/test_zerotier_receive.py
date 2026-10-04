#!/usr/bin/env python3
"""Exercise the production receive path without an emulator or real network."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "Source/dvlnet/protocol_zt.cpp").read_text()
header = (ROOT / "Source/dvlnet/protocol_zt.h").read_text()

def function(name):
    start = source.index("bool protocol_zt::" + name + "(")
    opening = source.index("\n{", start) + 1
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

constant = re.search(r"static constexpr uint32_t PKTBUF_LEN = [^;]+;", header)[0]
member = re.search(r"std::unique_ptr<[^;]+receiveBuffer[^;]+;", header)
member = member[0] if member else ""
prelude = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <deque>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
using buffer_t=std::vector<unsigned char>;
struct PacketError { const char *what() const { return "fake error"; } };
struct ZeroTierSocketAddress { struct { unsigned char s6_addr[16]; } sin6_addr; };
struct sockaddr {};
using socklen_t=unsigned;
int acceptCalls=0, udpCalls=0, tcpCalls=0;
std::deque<int> tcpResults, udpResults;
void *lastBuffer=nullptr;
int lwip_accept(int fd,sockaddr *,socklen_t *) { assert(fd>=0); ++acceptCalls; errno=EWOULDBLOCK; return -1; }
int lwip_close(int) { return 0; }
int fakeReceive(int fd,void *buffer,size_t size,std::deque<int> &results) {
    assert(fd>=0 && size==65536); lastBuffer=buffer;
    int result=results.empty() ? -1 : results.front();
    if (!results.empty()) results.pop_front();
    if (result<0) errno=EWOULDBLOCK;
    else std::fill_n(static_cast<unsigned char *>(buffer),result,0x5a);
    return result;
}
int lwip_recv(int fd,void *buffer,size_t size,int) { ++tcpCalls; return fakeReceive(fd,buffer,size,tcpResults); }
int lwip_recvfrom(int fd,void *buffer,size_t size,int,sockaddr *,socklen_t *) {
    ++udpCalls; return fakeReceive(fd,buffer,size,udpResults);
}
template <typename... Args> void Log(Args &&...) {}
template <typename... Args> void LogError(Args &&...) {}
template <typename... Args> void SDL_SetError(Args &&...) {}
struct Queue {
    std::deque<buffer_t> packets;
    void Write(buffer_t bytes) { packets.push_back(std::move(bytes)); }
    std::expected<bool,PacketError> PacketReady() { return !packets.empty(); }
    std::expected<buffer_t,PacketError> ReadPacket() { auto value=std::move(packets.front());packets.pop_front();return value; }
};
class protocol_zt {
public:
    struct endpoint {
        std::array<unsigned char,16> addr{};
        bool operator<(const endpoint &other) const { return addr<other.addr; }
    };
    struct peer_state { int fd=-1; Queue recv_queue; };
''' + constant + "\n" + member + r'''
    int fd_tcp=-1, fd_udp=-1;
    std::map<endpoint,peer_state> peer_list;
    std::deque<std::pair<endpoint,buffer_t>> oob_recv_queue;
    std::deque<endpoint> disconnect_queue;
    bool accept_all(); bool recv_peer(const endpoint &); bool recv_from_peers();
    bool recv_from_udp(); bool recv(endpoint &,buffer_t &);
    bool send_queued_all() { return true; }
    static void set_nonblock(int) {} static void set_nodelay(int) {}
};
'''
test = r'''
int main() {
    // This object also stays small when embedded in the heap-owned network provider.
    static_assert(sizeof(protocol_zt)<4096);
    protocol_zt protocol;
    protocol_zt::endpoint peer;
    buffer_t data;
    // The public-game list polls immediately after hero selection, before joining.
    assert(!protocol.recv(peer,data));
    assert(acceptCalls==0 && udpCalls==0 && tcpCalls==0);
    protocol.fd_tcp=1;
    assert(!protocol.recv(peer,data));
    assert(acceptCalls==0 && udpCalls==0);
    protocol.fd_udp=2;
    udpResults={65535};
    assert(protocol.recv(peer,data));
    assert(data.size()==65535 && data.front()==0x5a && data.back()==0x5a);
    void *buffer=lastBuffer;
    assert(buffer);
    udpResults={0};
    assert(protocol.recv(peer,data) && data.empty()); // UDP zero length is legal.
    assert(lastBuffer==buffer);
    protocol_zt::endpoint tcpPeer; tcpPeer.addr[0]=1;
    auto &state=protocol.peer_list[tcpPeer]; state.fd=3;
    tcpResults={65536,-1};
    assert(protocol.recv_peer(tcpPeer));
    assert(lastBuffer==buffer && state.recv_queue.packets.front().size()==65536);
    state.recv_queue.packets.clear();
    tcpResults={0};
    assert(!protocol.recv_peer(tcpPeer)); // Closed TCP stream must stop receiving.
    assert(state.recv_queue.packets.empty());
}
'''
with tempfile.TemporaryDirectory() as directory:
    temp=Path(directory)
    cpp=temp/"receive.cpp"
    cpp.write_text(prelude+"\n".join(function(name) for name in
        ("recv_peer","recv_from_peers","recv_from_udp","accept_all","recv"))+test)
    exe=temp/"receive"
    subprocess.run([os.environ.get("CXX","c++"),"-std=c++23","-O1","-Wall","-Wextra","-Werror",
        "-Wframe-larger-than=4096","-fsanitize=address,undefined",str(cpp),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
print("PASS: lobby polling before sockets, 64 KB receives on heap, buffer reuse and TCP EOF (ASan/UBSan)")
