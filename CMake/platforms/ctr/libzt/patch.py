"""Apply the 3DS adapter to the pinned libzt tree (only this target)."""
import pathlib, re, sys
root = pathlib.Path(sys.argv[1])
adapter = pathlib.Path(__file__).parent

def edit(name, fn):
    p = root / name
    p.write_text(fn(p.read_text()))

edit('CMakeLists.txt', lambda s: s.replace('if(UNIX)', 'if(UNIX OR NINTENDO_3DS)').replace('-DZT_USE_MINIUPNPC=1', '')
     .replace('option(BUILD_SHARED_LIB         "Build shared libary"         TRUE)', 'option(BUILD_SHARED_LIB         "Build shared libary"         FALSE)')
     .replace('option(ALLOW_INSTALL_TARGET     "Enable the install target"   TRUE)', 'option(ALLOW_INSTALL_TARGET     "Enable the install target"   FALSE)'))
# NAT-PMP / UPnP are optional; native UDP hole punching and relays remain active.
(root / 'ctr_optional_nat.c').write_text('/* No optional router port mapping on Nintendo 3DS. */\n')
edit('CMakeLists.txt', lambda s: re.sub(r'file\(GLOB libnatpmpSrcGlob.*?endif\(\)', 'set(libnatpmpSrcGlob "${PROJ_DIR}/ctr_optional_nat.c")', s, count=1, flags=re.S))
edit('CMakeLists.txt', lambda s: re.sub(r'file\(\s*GLOB\s*libminiupnpcSrcGlob.*?\)', 'set(libminiupnpcSrcGlob "${PROJ_DIR}/ctr_optional_nat.c")', s, count=1, flags=re.S))
# Include the adapter only in library implementation files, not application/std headers.
for name in ['ext/ZeroTierOne/node/Mutex.hpp', 'ext/ZeroTierOne/osdep/Thread.hpp',
             'src/Controls.cpp', 'ext/lwip-contrib/ports/unix/port/sys_arch.c']:
    edit(name, lambda s: s.replace('#include <pthread.h>', '#include <pthread.h>\n#include "zt_ctr_threads.h"') if '#include <pthread.h>' in s else '#include "zt_ctr_threads.h"\n' + s)
edit('ext/ZeroTierOne/node/Constants.hpp', lambda s: s.replace('// __LINUX__', '#ifdef __3DS__\n#define __UNIX_LIKE__\n#endif\n\n// __LINUX__', 1))
edit('ext/ZeroTierOne/osdep/Binder.hpp', lambda s: s.replace('#include <ifaddrs.h>', '#ifndef __3DS__\n#include <ifaddrs.h>\n#endif').replace('#if ! defined(ZT_SDK) || ! defined(__ANDROID__)', '#if !defined(__3DS__) && (!defined(ZT_SDK) || !defined(__ANDROID__))'))
# SOC supports sockets, not POSIX pipes. Bound the wait instead of allocating a pipe.
edit('ext/ZeroTierOne/osdep/Phy.hpp', lambda s: s.replace('inline void whack()\n\t{', 'inline void whack()\n\t{\n#ifdef __3DS__\n\t\treturn;\n#endif').replace('if (::pipe(pipes))', 'pipes[0] = pipes[1] = -1;\n#ifndef __3DS__\n\t\tif (::pipe(pipes))').replace('throw std::runtime_error("unable to create pipes for select() abort");\n#endif // Windows', 'throw std::runtime_error("unable to create pipes for select() abort");\n#endif\n#endif // Windows')
     .replace('inline void poll(unsigned long timeout)\n\t{', 'inline void poll(unsigned long timeout)\n\t{\n#ifdef __3DS__\n\t\ttimeout = (timeout == 0 || timeout > 10) ? 10 : timeout;\n#endif')
     .replace('FD_ISSET(_whackReceiveSocket,&rfds)', '(_whackReceiveSocket >= 0) && FD_ISSET(_whackReceiveSocket,&rfds)'))
# Strong randomness comes from the console service, never an emulated /dev file.
edit('ext/ZeroTierOne/node/Utils.cpp', lambda s: s.replace('#else // not __WINDOWS__\n\n\tstatic int devURandomFd', '#elif defined(__3DS__)\n\tif ((!(*psGetSessionHandle()) && R_FAILED(psInit())) || R_FAILED(PS_GenerateRandomBytes(buf, bytes)))\n\t\tthrow std::runtime_error("3DS secure random service failed");\n#else // not __WINDOWS__\n\n\tstatic int devURandomFd'))
edit('ext/ZeroTierOne/node/Utils.cpp', lambda s: '#include <3ds.h>\n' + s)
edit('ext/lwip-contrib/ports/unix/port/sys_arch.c', lambda s: s.replace('clock_gettime(CLOCK_MONOTONIC, ts);', 'ctr_zt_monotonic(ts);'))
