#!/usr/bin/env python3
"""Exercise the production release parser against nested and malformed responses."""
from pathlib import Path
import os, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[2]
TEST = r'''
#include <cassert>
#include "platform/ctr/update_json.hpp"
using namespace devilution;
int main() {
  assert(CtrJsonString(R"({"body":"release notes with tag_name", "assets":[{"tag_name":"evil"}], "tag_name":"v1.2.3"})", "tag_name") == "v1.2.3");
  assert(!CtrJsonString(R"({"assets":[{"tag_name":"nested"}]})", "tag_name"));
  assert(!CtrJsonString(R"({"tag_name":42})", "tag_name"));
  assert(CtrJsonString(R"({"tag_name":"valid"})", "tag_name") == "valid");
  assert(!CtrJsonString(R"({"tag_name":"v1.2")", "tag_name"));
  assert(!CtrJsonString(R"({"tag_name":"v1"} {"tag_name":"v2"})", "tag_name"));
  assert(!CtrJsonString(R"([{"status":"ahead"}])", "status"));
  assert(CtrJsonString(R"({"commits":[{"status":"behind"}],"status":"ahead"})", "status") == "ahead");
  assert(CtrJsonString(R"({"url":"https:\/\/github.com"})", "url") == "https://github.com");
  assert(!CtrValidReleaseTag("v1/../../other"));
  assert(!CtrValidReleaseTag(""));
  assert(!CtrValidReleaseTag(std::string(101, 'a')));
  assert(CtrValidReleaseTag("3ds-v1.6.0-test_2"));
  std::string large = "{\"assets\":[";
  for (int i = 0; i < 500; ++i) large += (i ? "," : "") + std::string("{\"tag_name\":\"nested\"}");
  large += "],\"tag_name\":\"v9.0\"}";
  assert(CtrJsonString(large, "tag_name") == "v9.0");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'test.cpp'
    exe = Path(tmp) / 'test'
    source.write_text(TEST)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT/'Source'), '-I'+str(ROOT/'3rdParty/jsmn'), str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: release parser handles nested, large and malformed responses')
