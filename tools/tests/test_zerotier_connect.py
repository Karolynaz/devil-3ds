#!/usr/bin/env python3
"""Exercise production peer connection/pending/partial-send paths without networking."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT/'Source/dvlnet/protocol_zt.cpp').read_text()


def function(signature):
    start = source.index(signature)
    opening = source.index('\n{', start)+1
    end, depth = opening+1, 1
    while depth:
        depth += (source[end] == '{')-(source[end] == '}')
        end += 1
    return source[start:end]


HARNESS = r'''
#define __3DS__ 1
#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <vector>
using buffer_t=std::vector<unsigned char>;
struct PacketError { std::string message;const char *what() const {return message.c_str();} };
template<class... A> PacketError ProtocolError(std::string_view text,A&&...) {return {std::string(text)};}
template<class... A> void LogError(A&&...) {}
struct sockaddr {};
using socklen_t=unsigned;
struct ZeroTierSocketAddress {unsigned sin6_port,sin6_family;struct {unsigned char s6_addr[16]{};} sin6_addr;};
constexpr int AF_INET6=10, SOCK_STREAM=1, SOL_SOCKET=2, SO_ERROR=3;
#undef htons
unsigned htons(unsigned port) {return port;}
uint32_t now=0;
uint32_t SDL_GetTicks() {return now;}
bool socketWorks=true,nonblockWorks=true,peerReady=false;
int connectError=EINPROGRESS, socketError=0, closes=0, sends=0, socketCalls=0, sendError=EAGAIN;
std::deque<int> sendResults;
buffer_t transmitted;
int lwip_socket(int,int,int) {++socketCalls;if(!socketWorks){errno=EMFILE;return -1;}return 7;}
int lwip_connect(int fd,const sockaddr*,unsigned) {assert(fd==7);errno=connectError;return connectError ? -1 : 0;}
int lwip_getsockopt(int fd,int,int,void *out,socklen_t*) {assert(fd==7);*(int*)out=socketError;return 0;}
int lwip_getpeername(int fd,sockaddr*,socklen_t*) {assert(fd==7);errno=ENOTCONN;return peerReady ? 0 : -1;}
int lwip_send(int fd,const void *data,size_t length,int) {
    assert(fd==7 && !sendResults.empty());++sends;
    int n=sendResults.front();sendResults.pop_front();
    if(n<0){errno=sendError;return -1;}
    assert((size_t)n<=length);auto *p=static_cast<const unsigned char*>(data);
    transmitted.insert(transmitted.end(),p,p+n);return n;
}
int lwip_close(int fd) {assert(fd==7);++closes;return 0;}
class protocol_zt {
public:
    struct endpoint {std::array<unsigned char,16> addr{};bool operator<(const endpoint &other) const{return addr<other.addr;}};
    struct peer_state {int fd=-1;bool connecting=false;uint32_t connectStarted=0;std::deque<buffer_t> send_queue;};
    std::map<endpoint,peer_state> peer_list;
    std::deque<endpoint> disconnect_queue;
    unsigned default_port=6112;
    static void set_nodelay(int fd) {assert(fd>=0);}
    static bool set_nonblock(int fd) {assert(fd>=0);errno=EIO;return nonblockWorks;}
    std::expected<bool,PacketError> send_queued_peer(const endpoint &);
    bool send_queued_all();bool is_peer_connected(endpoint &);
};
'''
TEST = r'''
int main() {
    protocol_zt protocol;protocol_zt::endpoint peer;
    auto &state=protocol.peer_list[peer];
    assert(protocol.send_queued_all() && socketCalls==0); // No idle sockets.
    state.send_queue.push_back({1,2,3,4});
    protocol.send_queued_all();assert(state.fd==7 && state.connecting && sends==0);
    assert(!protocol.is_peer_connected(peer) && protocol.disconnect_queue.empty());
    now=1000;protocol.send_queued_all();assert(sends==0 && state.send_queue.front().size()==4);
    peerReady=true;sendResults={2};protocol.send_queued_all();
    assert(!state.connecting && protocol.is_peer_connected(peer));
    assert((state.send_queue.front()==buffer_t{3,4}));
    sendResults={-1};protocol.send_queued_all(); // Backpressure preserves data.
    assert(state.fd==7 && state.send_queue.front().size()==2 && protocol.disconnect_queue.empty());
    sendResults={2};protocol.send_queued_all();
    assert(state.send_queue.empty() && (transmitted==buffer_t{1,2,3,4}));
    state.send_queue.push_back({5});sendError=EPIPE;sendResults={-1};
    protocol.send_queued_all();assert(state.fd==-1 && state.send_queue.empty() && closes==1);
    assert(protocol.disconnect_queue.size()==1);
    protocol.send_queued_all();assert(closes==1 && protocol.disconnect_queue.size()==1);
    protocol.disconnect_queue.clear();
    socketWorks=false;state.send_queue.push_back({1});protocol.send_queued_all();
    assert(state.fd==-1 && state.send_queue.empty() && protocol.disconnect_queue.size()==1);
    socketWorks=true;protocol.disconnect_queue.clear();
    nonblockWorks=false;state.send_queue.push_back({1});protocol.send_queued_all();
    assert(state.fd==-1 && closes==2 && protocol.disconnect_queue.size()==1);
    nonblockWorks=true;protocol.disconnect_queue.clear();
    connectError=ECONNREFUSED;state.send_queue.push_back({1});protocol.send_queued_all();
    assert(state.fd==-1 && closes==3 && protocol.disconnect_queue.size()==1);
    connectError=EINPROGRESS;peerReady=false;protocol.disconnect_queue.clear();
    now=0xfffffff0;state.send_queue.push_back({1});protocol.send_queued_all();
    now=10;protocol.send_queued_all();assert(state.connecting && state.fd==7); // Clock wraps safely.
    now=31000;protocol.send_queued_all();assert(state.connecting && state.fd==7);
    now=61000;protocol.send_queued_all();assert(state.fd==-1 && closes==4 && protocol.disconnect_queue.size()==1);
    protocol.disconnect_queue.clear();now=0;state.send_queue.push_back({1});protocol.send_queued_all();
    socketError=ECONNREFUSED;protocol.send_queued_all();
    assert(state.fd==-1 && closes==5 && protocol.disconnect_queue.size()==1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory)/'connect.cpp'
    exe = Path(directory)/'connect'
    functions = '\n'.join(function(signature) for signature in (
        'std::expected<bool, PacketError> protocol_zt::send_queued_peer(',
        'bool protocol_zt::send_queued_all(', 'bool protocol_zt::is_peer_connected('))
    cpp.write_text(HARNESS+functions+TEST)
    subprocess.run(['c++', '-std=c++23', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: pending connects, failures/timeouts, clock wrap, partial sends and one disconnect event (ASan/UBSan)')
