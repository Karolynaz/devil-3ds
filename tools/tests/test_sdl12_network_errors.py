#!/usr/bin/env python3
"""Exercise production error calls with SDL 1.2's actual formatter.

Pass upstream SDL-1.2/src/SDL_error.c as argv[1].
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
formatter = source[source.index('static const char *SDL_LookupString'):source.index('/* Very common errors')]
calls = []
for file in ['Source/dvlnet/base_protocol.h', 'Source/dvlnet/tcp_client.cpp']:
    for call in re.findall(r'SDL_SetError\([^;]+;', Path(file).read_text()):
        if 'message)' in call or 'message.' in call:
            calls.append(call)
assert len(calls) == 9
code = r'''
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#define SDL_arraysize(a) (sizeof(a)/sizeof((a)[0]))
#define SDL_snprintf std::snprintf
#define ERR_MAX_STRLEN 256
#define ERR_MAX_ARGS 8
#define SDL_ERRBUFIZE 1024
struct SDL_error {
 int error, argc; char key[256];
 union { long value_l; int value_i; double value_f; void *value_ptr; char buf[256]; } args[8];
};
SDL_error errorBuffer{};
SDL_error *SDL_GetErrBuf() { return &errorBuffer; }
size_t SDL_strlcpy(char *out, const char *in, size_t size) {
 auto length=std::strlen(in); if (size) std::snprintf(out,size,"%s",in); return length;
}
''' + formatter + r'''
int main() {
 // Demonstrate the SDL 1.2 incompatibility that caused the visible "s".
 SDL_SetError("%.*s", 4, "test"); assert(std::string(SDL_GetError()) == "s");
 const std::string storage="Connection reset by peer: ignored trailing text";
 const std::string_view message(storage.data(), 24);
'''
for call in calls:
    code += call + '\nassert(std::string(SDL_GetError()).find(std::string(message)) != std::string::npos);\n'
code += '}\n'
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root/'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined',
                    str(root/'test.cpp'), '-o', str(root/'test')], check=True)
    subprocess.run([str(root/'test')], check=True)
print('PASS: all nine production network errors survive real SDL 1.2 formatting and bounded string views')
