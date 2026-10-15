#!/usr/bin/env python3
"""Run production TCP polling after async_connect exhausts the ASIO context.

Pass the pinned ASIO source directory (containing asio/include) as argv[1].
"""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path('Source/dvlnet/tcp_client.cpp').read_text()
start = source.index('std::expected<void, PacketError> tcp_client::poll()')
end = source.index('\nvoid tcp_client::HandleReceive', start)
poll = source[start:end]
code = r'''
#include <asio.hpp>
#include <cassert>
#include <chrono>
#include <expected>
#include <optional>
#include <thread>
using PacketError = int;
struct Server { std::expected<void, PacketError> CheckIoHandlerError() { return {}; } };
struct tcp_client {
 asio::io_context ioc;
 std::optional<PacketError> ioHandlerResult;
 Server *local_server = nullptr;
 bool IsGameHost() { return local_server != nullptr; }
 std::expected<void, PacketError> poll();
};
''' + poll + r'''
int main() {
 tcp_client client;
 asio::io_context host;
 asio::ip::tcp::acceptor listener(host, {asio::ip::address_v4::loopback(), 0});
 asio::ip::tcp::socket peer(host), socket(client.ioc);
 bool accepted = false, connected = false;
 listener.async_accept(peer, [&](asio::error_code error) { assert(!error); accepted = true; });
 auto endpoints = asio::ip::tcp::resolver(client.ioc).resolve("127.0.0.1", std::to_string(listener.local_endpoint().port()));
 asio::async_connect(socket, endpoints, [&](asio::error_code error, const asio::ip::tcp::endpoint &) {
  assert(!error); connected = true;
 });
 const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
 while (!connected || !accepted) {
  assert(std::chrono::steady_clock::now() < deadline);
  client.ioc.poll(); host.poll(); std::this_thread::yield();
 }
 // This is the actual post-connect state for a joining client with no server.
 assert(client.ioc.stopped());
 char request = 'J', response = 'A', received = 0;
 bool sent = false, replied = false;
 asio::async_write(socket, asio::buffer(&request, 1), [&](asio::error_code error, size_t size) {
  assert(!error && size == 1); sent = true;
 });
 socket.async_receive(asio::buffer(&received, 1), [&](asio::error_code error, size_t size) {
  assert(!error && size == 1); replied = true;
 });
 asio::write(peer, asio::buffer(&response, 1));
 while (!sent || !replied) {
  assert(std::chrono::steady_clock::now() < deadline);
  assert(client.poll()); std::this_thread::yield();
 }
 char hostReceived = 0;
 asio::read(peer, asio::buffer(&hostReceived, 1));
 assert(hostReceived == request && received == response);
}
'''
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    cpp, binary = root/'test.cpp', root/'test'
    cpp.write_text(code)
    subprocess.run(['c++', '-std=c++23', '-pthread', '-fsanitize=address,undefined',
                    '-I'+str(Path(sys.argv[1])/'asio/include'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
print('PASS: real TCP loopback sends and receives after async_connect exhausts ASIO (ASan/UBSan)')
