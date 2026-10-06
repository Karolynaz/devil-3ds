#!/usr/bin/env python3
"""Check the concurrent ZeroTier diagnostic counters without console/emulator."""
from pathlib import Path
import subprocess, tempfile
source = Path('Source/dvlnet/zerotier_native.cpp').read_text()
trace = source[source.index('namespace {\nstd::atomic_int CtrZtDiagnostics'):source.index('\n#endif\n\nnamespace devilution')]
start = source.index('void zerotier_log_diagnostics(FILE *file)')
log = source[start:source.index('\nCtrZeroTierStartup zerotier_startup_state()', start)]
code = '#include <atomic>\n#include <cstdio>\n#include <cassert>\n#include <thread>\n#include <string>\n' + trace + '\n' + log + r'''
int main() {
 std::thread a([] { for(int i=0;i<10000;++i) ctr_zt_trace(3,0); });
 std::thread b([] { for(int i=0;i<10000;++i) ctr_zt_trace(3,0); });
 a.join(); b.join();
 ctr_zt_trace(0,4); ctr_zt_trace(1,200); ctr_zt_trace(6,11); ctr_zt_trace(7,12);
 ctr_zt_trace(100,999);
 assert(CtrZtDiagnostics[3]==20000);
 FILE *f=tmpfile(); assert(f); zerotier_log_diagnostics(f); rewind(f);
 char text[512]; assert(fgets(text,sizeof(text),f)); fclose(f);
 std::string s(text);
 assert(s.find("phase 4; event 200")!=std::string::npos);
 assert(s.find("sends 20000")!=std::string::npos);
 assert(s.find("socket errno 11; worker error 12")!=std::string::npos);
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory); (p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-pthread','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: concurrent counters, bounds and log formatting (ASan/UBSan)')
