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
    def port_threads(s):
        # Change only library identifiers. Macros here would corrupt STL's gthreads.
        names = {'pthread_t': 'ctr_zt_thread_t', 'pthread_attr_t': 'ctr_zt_attr_t', 'pthread_mutex_t': 'ctr_zt_mutex_t', 'pthread_mutexattr_t': 'ctr_zt_mutexattr_t', 'pthread_cond_t': 'ctr_zt_cond_t', 'pthread_condattr_t': 'ctr_zt_condattr_t', 'pthread_mutex_init': 'ctr_zt_mutex_init', 'pthread_mutex_destroy': 'ctr_zt_mutex_destroy', 'pthread_mutex_lock': 'ctr_zt_mutex_lock', 'pthread_mutex_unlock': 'ctr_zt_mutex_unlock', 'pthread_mutexattr_init': 'ctr_zt_mutexattr_init', 'pthread_mutexattr_settype': 'ctr_zt_mutexattr_settype', 'pthread_cond_init': 'ctr_zt_cond_init', 'pthread_cond_destroy': 'ctr_zt_cond_destroy', 'pthread_condattr_init': 'ctr_zt_condattr_init', 'pthread_condattr_destroy': 'ctr_zt_condattr_destroy', 'pthread_condattr_setclock': 'ctr_zt_condattr_setclock', 'pthread_cond_wait': 'ctr_zt_cond_wait', 'pthread_cond_timedwait': 'ctr_zt_cond_timedwait', 'pthread_cond_broadcast': 'ctr_zt_cond_broadcast', 'pthread_create': 'ctr_zt_create', 'pthread_join': 'ctr_zt_join', 'pthread_attr_init': 'ctr_zt_attr_init', 'pthread_attr_setstacksize': 'ctr_zt_attr_setstacksize', 'pthread_attr_destroy': 'ctr_zt_attr_destroy', 'pthread_self': 'threadGetCurrent', 'pthread_exit': 'ctr_zt_exit'}
        names['PTHREAD_MUTEX_INITIALIZER'] = 'CTR_ZT_MUTEX_INITIALIZER'
        s = re.sub(r'\b(?:' + '|'.join(names) + r')\b', lambda m: names[m[0]], s)
        return '#include "zt_ctr_threads.h"\n' + s
    edit(name, port_threads)
edit('ext/ZeroTierOne/node/Constants.hpp', lambda s: s.replace('// __LINUX__', '#ifdef __3DS__\n#define __UNIX_LIKE__\n#endif\n\n// __LINUX__', 1))
edit('ext/ZeroTierOne/osdep/Binder.hpp', lambda s: s.replace('#include <ifaddrs.h>', '#ifndef __3DS__\n#include <ifaddrs.h>\n#endif').replace('#if ! defined(ZT_SDK) || ! defined(__ANDROID__)', '#if !defined(__3DS__) && (!defined(ZT_SDK) || !defined(__ANDROID__))'))
# SOC supports sockets, not POSIX pipes. Bound the wait instead of allocating a pipe.
edit('ext/ZeroTierOne/osdep/Phy.hpp', lambda s: s.replace('inline void whack()\n\t{', 'inline void whack()\n\t{\n#ifdef __3DS__\n\t\treturn;\n#endif').replace('if (::pipe(pipes))', 'pipes[0] = pipes[1] = -1;\n#ifndef __3DS__\n\t\tif (::pipe(pipes))').replace('throw std::runtime_error("unable to create pipes for select() abort");\n#endif // Windows', 'throw std::runtime_error("unable to create pipes for select() abort");\n#endif\n#endif // Windows')
     .replace('inline void poll(unsigned long timeout)\n\t{', 'inline void poll(unsigned long timeout)\n\t{\n#ifdef __3DS__\n\t\ttimeout = (timeout == 0 || timeout > 10) ? 10 : timeout;\n#endif')
     .replace('FD_ISSET(_whackReceiveSocket,&rfds)', '(_whackReceiveSocket >= 0) && FD_ISSET(_whackReceiveSocket,&rfds)'))
# Strong randomness comes from the console service, never an emulated /dev file.
edit('ext/ZeroTierOne/node/Utils.cpp', lambda s: s.replace('#else // not __WINDOWS__\n\n\tstatic int devURandomFd', '#elif defined(__3DS__)\n\tif ((!psGetSessionHandle() && R_FAILED(psInit())) || R_FAILED(PS_GenerateRandomBytes(buf, bytes)))\n\t\tthrow std::runtime_error("3DS secure random service failed");\n#else // not __WINDOWS__\n\n\tstatic int devURandomFd'))
edit('ext/ZeroTierOne/node/Utils.cpp', lambda s: '#include <3ds.h>\n' + s)
edit('ext/lwip-contrib/ports/unix/port/sys_arch.c', lambda s: s.replace('clock_gettime(CLOCK_MONOTONIC, ts);', 'ctr_zt_monotonic(ts);'))

# SOC has no IPv6 transport or Unix-domain sockets. The virtual network remains
# IPv6 through lwIP, carried over actual IPv4 UDP sockets on the console.
def port_phy(s):
    s = s.replace('#ifdef __UNIX_LIKE__', '#if defined(__UNIX_LIKE__) && !defined(__3DS__)')
    return re.sub(r'([^\n]*setsockopt\(s,IPPROTO_IPV6,IPV6_V6ONLY[^\n]*)', r'#ifndef __3DS__\n\1\n#endif', s)
edit('ext/ZeroTierOne/osdep/Phy.hpp', port_phy)
edit('ext/ZeroTierOne/node/InetAddress.cpp', lambda s: '#ifndef INET6_ADDRSTRLEN\n#define INET6_ADDRSTRLEN 46\n#endif\n' + s)
edit('ext/ZeroTierOne/node/Bond.cpp', lambda s: s.replace('std::min(_paths[pathIdx].packetsReceivedSinceLastQoS, ZT_QOS_TABLE_SIZE)', 'std::min<int32_t>(_paths[pathIdx].packetsReceivedSinceLastQoS, ZT_QOS_TABLE_SIZE)'))
# lwIP virtual descriptors must use their own 1024-bit set, not SOC's fd_set.
for name in ['ext/lwip/src/include/lwip/sockets.h', 'ext/lwip/src/include/lwip/priv/sockets_priv.h',
             'ext/lwip/src/api/sockets.c', 'src/Sockets.cpp']:
    names = {n: 'ctr_lwip_' + n for n in ['fd_set', 'FD_SET', 'FD_CLR', 'FD_ISSET', 'FD_ZERO', 'FD_SETSIZE']}
    edit(name, lambda s: re.sub(r'\b(?:' + '|'.join(names) + r')\b', lambda m: names[m[0]], s))
# The virtual-tap thread only waits for shutdown. Use a native event instead of
# a nonexistent pipe; it sleeps without polling and wakes before join().
edit('src/VirtualTap.hpp', lambda s: s.replace('int _shutdownSignalPipe[2]', '#ifdef __3DS__\n    LightEvent _shutdownEvent;\n#endif\n    int _shutdownSignalPipe[2]'))
edit('src/VirtualTap.cpp', lambda s: s.replace('#ifndef __WINDOWS__', '#if !defined(__WINDOWS__) && !defined(__3DS__)')
     .replace('OSUtils::ztsnprintf(vtap_full_name, VTAP_NAME_LEN, "libzt-vtap-%llx", _net_id);', 'OSUtils::ztsnprintf(vtap_full_name, VTAP_NAME_LEN, "libzt-vtap-%llx", _net_id);\n#ifdef __3DS__\n    LightEvent_Init(&_shutdownEvent, RESET_STICKY);\n#endif')
     .replace('_run = false;', '_run = false;\n#ifdef __3DS__\n    LightEvent_Signal(&_shutdownEvent);\n#endif')
     .replace('void VirtualTap::threadMain() throw()\n{', 'void VirtualTap::threadMain() throw()\n{\n#ifdef __3DS__\n    LightEvent_Wait(&_shutdownEvent);\n    return;\n#endif'))

# Virtual DNS error state must not collide with libctru's actual SOC resolver.
for name in ['ext/lwip/src/include/lwip/netdb.h', 'ext/lwip/src/api/netdb.c']:
    edit(name, lambda s: re.sub(r'\bh_errno\b', 'ctr_lwip_h_errno', s))
# Virtual IPv6 has a different sockaddr layout from the native ASIO shim.
# Distinct type names keep LTO from treating the two layouts as one C++ type.
for name in ['ext/lwip/src/include/lwip/inet.h', 'ext/lwip/src/include/lwip/sockets.h',
             'ext/lwip/src/core/ipv6/inet6.c', 'ext/lwip/src/netif/ppp/ipv6cp.c']:
    names = {n: 'ctr_lwip_' + n for n in ['in6_addr', 'sockaddr_in6']}
    edit(name, lambda s: re.sub(r'\b(?:' + '|'.join(names) + r')\b', lambda m: names[m[0]], s))
