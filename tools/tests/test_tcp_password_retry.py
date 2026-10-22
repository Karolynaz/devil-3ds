#!/usr/bin/env python3
"""Check production config copying and the typed password-required flag."""
from pathlib import Path
import tempfile,subprocess
s=Path('Source/dvlnet/base.cpp').read_text()
f=s[s.index('void base::copy_session_configuration_to'):s.index('void base::setup_gameinfo')]
s=Path('Source/dvlnet/tcp_client.cpp').read_text()
start=s.index('void tcp_client::RaiseIoHandlerError(')
g=s[start:s.index('\ntcp_client::~tcp_client()',start)]
code=r'''
#include <cassert>
#include <map>
#include <vector>
#include <optional>
struct abstract_net {
 std::vector<int> info; std::map<int,int> handlers;
 void setup_gameinfo(std::vector<int> v) { info=v; }
 void SNetRegisterEventHandler(int event,int callback) { handlers[event]=callback; }
};
struct base { std::vector<int> game_init_info; std::map<int,int> registered_handlers;
 void copy_session_configuration_to(abstract_net &target) const;
};
struct PacketError {
 enum class ErrorCode { None, DecryptionFailed };
 ErrorCode value;
 ErrorCode code() const {return value;}
};
struct tcp_client {
 bool passwordRequired=false; std::optional<PacketError> ioHandlerResult;
 void RaiseIoHandlerError(const PacketError &error);
};
'''+f+g+r'''
int main() {
 base old{{11,22,33},{{1,100},{2,200},{3,300}}};
 abstract_net fresh;old.copy_session_configuration_to(fresh);
 assert(fresh.info==old.game_init_info && fresh.handlers==old.registered_handlers);
 tcp_client client;
 client.RaiseIoHandlerError({PacketError::ErrorCode::None});assert(!client.passwordRequired);
 client.RaiseIoHandlerError({PacketError::ErrorCode::DecryptionFailed});assert(client.passwordRequired);
 assert(client.ioHandlerResult->code()==PacketError::ErrorCode::DecryptionFailed);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: password error classification and event/config preservation on TCP retry')
