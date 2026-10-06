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
# Publish running flags before starting threads: the callback may run immediately
# and exit if its flag is still clear. Return failures instead of silently
# pretending that a missing callback/service thread started successfully.
def port_node_start(s):
    start = s.index('int zts_node_start()\n')
    end = s.index('\nint zts_node_is_online()', start)
    return s[:start] + '''int zts_node_start()
{
    ACQUIRE_SERVICE_OFFLINE();
    zts_lwip_driver_init();
    ctr_zt_thread_t callback_thread = NULL;
    if (zts_events->hasCallback()) {
        zts_events->setState(ZTS_STATE_CALLBACKS_RUNNING);
        // Keep the handle until the service is started, so a failure can
        // stop and join this callback before the user retries.
        ctr_zt_attr_t callback_stack = 256 * 1024;
        if (ctr_zt_create(&callback_thread, &callback_stack, cbRun, NULL) != 0) {
            zts_events->clrState(ZTS_STATE_CALLBACKS_RUNNING);
            zts_events->clrCallback();
            return ZTS_ERR_GENERAL;
        }
    }
    zts_events->setState(ZTS_STATE_NODE_RUNNING);
    ctr_zt_thread_t service_thread;
    if (ctr_zt_create(&service_thread, NULL, _runNodeService, NULL) != 0) {
        zts_events->clrState(ZTS_STATE_NODE_RUNNING);
        zts_events->clrState(ZTS_STATE_CALLBACKS_RUNNING);
        ctr_zt_join(callback_thread, NULL);
        return ZTS_ERR_GENERAL;
    }
    if (callback_thread)
        threadDetach(callback_thread);
    return ZTS_ERR_OK;
}
''' + s[end:]
edit('src/Controls.cpp', port_node_start)
edit('ext/ZeroTierOne/node/Constants.hpp', lambda s: s.replace('// __LINUX__', '#ifdef __3DS__\n#define __UNIX_LIKE__\n#endif\n\n// __LINUX__', 1))
edit('ext/ZeroTierOne/osdep/Binder.hpp', lambda s: s.replace('#include <ifaddrs.h>', '#ifndef __3DS__\n#include <ifaddrs.h>\n#endif').replace('#if ! defined(ZT_SDK) || ! defined(__ANDROID__)', '#if !defined(__3DS__) && (!defined(ZT_SDK) || !defined(__ANDROID__))'))
# Without getifaddrs, Binder must take its wildcard fallback. Leaving the
# flag true creates zero physical sockets, so the node never reaches ONLINE.
edit('ext/ZeroTierOne/osdep/Binder.hpp', lambda s: s.replace(
    'bool interfacesEnumerated = true;',
    '#ifdef __3DS__\n\t\tbool interfacesEnumerated = false;\n#else\n\t\tbool interfacesEnumerated = true;\n#endif')
    .replace('localIfAddrs.insert(std::pair<InetAddress, std::string>(InetAddress((const void*)',
             '#ifndef __3DS__\n\t\t\t\tlocalIfAddrs.insert(std::pair<InetAddress, std::string>(InetAddress((const void*)')
    .replace('16, ports[x]), std::string()));', '16, ports[x]), std::string()));\n#endif'))
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
             'ext/lwip/src/core/ipv6/inet6.c', 'ext/lwip/src/netif/ppp/ipv6cp.c',
             'ext/lwip/src/api/sockets.c', 'ext/lwip/src/api/netdb.c']:
    names = {n: 'ctr_lwip_' + n for n in ['in6_addr', 'sockaddr_in6']}
    edit(name, lambda s: re.sub(r'\b(?:' + '|'.join(names) + r')\b', lambda m: names[m[0]], s))

# libctru unmaps the entire application heap before svcExitProcess. Detached
# ZeroTier/lwIP threads would continue running on freed heap-backed stacks.
# Keep joinable handles and provide one application-exit-only shutdown entry.
def port_node_shutdown(s):
    s = s.replace('NodeService* zts_service;',
                  'static ::Thread ctr_node_thread = NULL;\nstatic ::Thread ctr_callback_thread = NULL;\nNodeService* zts_service;')
    s = s.replace('ctr_zt_thread_t callback_thread = NULL;', 'ctr_callback_thread = NULL;')
    s = s.replace('&callback_thread, &callback_stack', '&ctr_callback_thread, &callback_stack')
    s = s.replace('ctr_zt_thread_t service_thread;', 'ctr_zt_attr_t service_stack = 1024 * 1024;')
    s = s.replace('&service_thread, NULL, _runNodeService', '&ctr_node_thread, &service_stack, _runNodeService')
    s = s.replace('ctr_zt_join(callback_thread, NULL);',
                  'ctr_zt_join(ctr_callback_thread, NULL);\n        ctr_callback_thread = NULL;')
    s = s.replace('    if (callback_thread)\n        threadDetach(callback_thread);\n', '')
    start = s.index('int zts_node_free()\n')
    s = s[:start] + '''// This private entry is called only when the application is exiting.
void zts_ctr_shutdown()
{
    {
        Mutex::Lock lock(service_m);
        if (zts_service)
            zts_service->terminate();
    }
    // The service destroys taps; tap destructors need a live TCP/IP core.
    ctr_zt_join(ctr_node_thread, NULL);
    ctr_node_thread = NULL;
    if (zts_events) {
        zts_lwip_driver_shutdown();
        ctr_tcpip_shutdown();
        ctr_zt_join_sys_threads();
        zts_events->clrState(ZTS_STATE_CALLBACKS_RUNNING);
    }
    // Callbacks can still own queued event data and application references.
    ctr_zt_join(ctr_callback_thread, NULL);
    ctr_callback_thread = NULL;
    delete zts_service; // Handles initialization/service-start failure too.
    zts_service = NULL;
    delete zts_events;
    zts_events = NULL;
}

''' + s[start:]
    return s
edit('src/Controls.cpp', port_node_shutdown)

# Closing HOME immediately after startup must not let run() re-enable a node
# already asked to stop. Avoid clearing strings/identity fields concurrently
# with the service thread; it owns those fields until joined.
edit('src/NodeService.hpp', lambda s: s.replace('volatile bool _run;',
     'volatile bool _run;\n#ifdef __3DS__\n    bool _ctrStopping = false;\n#endif'))
edit('src/NodeService.cpp', lambda s: s.replace('    _run = true;\n    try {',
     '''    {
        Mutex::Lock lock(_run_m);
        if (_ctrStopping)
            return ONE_NORMAL_TERMINATION;
        _run = true;
    }
    try {''', 1).replace('void NodeService::terminate()\n{',
     '''void NodeService::terminate()
{
#ifdef __3DS__
    {
        Mutex::Lock lock(_run_m);
        _ctrStopping = true;
        _run = false;
    }
    _phy.whack();
    return;
#endif''', 1))

# lwIP's stock TCP/IP thread runs forever. Queue a final callback, release its
# core lock and return normally; never kill a thread while it owns a mutex.
def port_tcpip_shutdown(s):
    s = s.replace('static sys_mbox_t tcpip_mbox;',
                  'static sys_mbox_t tcpip_mbox;\nstatic int ctr_tcpip_initialized;\nstatic int ctr_tcpip_stopping;')
    s = s.replace('    tcpip_thread_handle_msg(msg);\n  }',
                  '''    tcpip_thread_handle_msg(msg);
    if (ctr_tcpip_stopping) {
      UNLOCK_TCPIP_CORE();
      return;
    }
  }''', 1)
    s = s.replace('  sys_thread_new(TCPIP_THREAD_NAME,',
                  '  ctr_tcpip_initialized = 1;\n  sys_thread_new(TCPIP_THREAD_NAME,', 1)
    s += '''
static void ctr_tcpip_stop_callback(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  ctr_tcpip_stopping = 1;
}

void ctr_tcpip_shutdown(void)
{
  static struct tcpip_msg shutdown_message;
  if (ctr_tcpip_initialized) {
    ctr_tcpip_initialized = 0;
    // No allocation: application exit must work even when memory is full.
    shutdown_message.type = TCPIP_MSG_CALLBACK_STATIC;
    shutdown_message.msg.cb.function = ctr_tcpip_stop_callback;
    shutdown_message.msg.cb.ctx = NULL;
    sys_mbox_post(&tcpip_mbox, &shutdown_message);
  }
}
'''
    return s
edit('ext/lwip/src/api/tcpip.c', port_tcpip_shutdown)

# The sys_arch list contains the driver and TCP/IP workers. Retain ownership
# until both have returned; freeing detached handles would be a use-after-free.
def port_sys_thread_shutdown(s):
    s = s.replace('  code = ctr_zt_create(&tmp,\n                        NULL,',
                  '  ctr_zt_attr_t stack = 1024 * 1024;\n  code = ctr_zt_create(&tmp,\n                        &stack,', 1)
    s += '''
void ctr_zt_join_sys_threads(void)
{
  struct sys_thread *thread;
  ctr_zt_mutex_lock(&threads_mutex);
  thread = threads;
  threads = NULL;
  ctr_zt_mutex_unlock(&threads_mutex);
  while (thread) {
    struct sys_thread *next = thread->next;
    ctr_zt_join(thread->pthread, NULL);
    free(thread);
    thread = next;
  }
}
'''
    return s
edit('ext/lwip-contrib/ports/unix/port/sys_arch.c', port_sys_thread_shutdown)
edit('src/Controls.cpp', lambda s: s.replace('#include "zt_ctr_threads.h"',
     '#include "zt_ctr_threads.h"\nextern "C" void ctr_tcpip_shutdown(void);\nextern "C" void ctr_zt_join_sys_threads(void);', 1))

# Wait for lwIP startup before publishing node startup success. Otherwise
# shutdown can clear STACK_RUNNING before its init callback sets it again.
edit('src/VirtualTap.cpp', lambda s: '#include <atomic>\n' + s.replace(
    'bool _has_exited = false;\nbool _has_started = false;',
    'std::atomic_bool _has_exited(false);\nstd::atomic_bool _has_started(false);').replace(
    '    sys_sem_wait(&sem);\n    // Main loop',
    '    sys_sem_wait(&sem);\n    sys_sem_free(&sem);\n    // Main loop').replace(
    '        DEFAULT_THREAD_PRIO);\n}',
    '        DEFAULT_THREAD_PRIO);\n    while (!_has_started)\n        zts_util_delay(1);\n}', 1))

# Diagnose the physical transport without storing addresses, identities or packets.
def diagnostic_replace(s, old, new):
    if old not in s:
        raise RuntimeError("Pinned libzt diagnostic location changed: " + old[:70])
    return s.replace(old, new, 1)
def trace_node(s):
    s = diagnostic_replace(s, '_node = new Node(this, (void*)0, &cb, OSUtils::now());',
        '#ifdef __3DS__\n            ctr_zt_trace(0, 1);\n#endif\n            _node = new Node(this, (void*)0, &cb, OSUtils::now());\n#ifdef __3DS__\n            ctr_zt_trace(0, 2);\n#endif')
    s = diagnostic_replace(s, '_binder.refresh(_phy, p, pc, explicitBind, *this);',
        '#ifdef __3DS__\n                ctr_zt_trace(0, 3);\n#endif\n                _binder.refresh(_phy, p, pc, explicitBind, *this);\n#ifdef __3DS__\n                ctr_zt_trace(0, 4);\n#endif')
    return s
edit('src/NodeService.cpp', trace_node)
def trace_phy(s):
    s = '#ifdef __3DS__\n#include "zt_ctr_threads.h"\n#endif\n' + s
    s = diagnostic_replace(s, 'if (!ZT_PHY_SOCKFD_VALID(s))\n\t\t\treturn (PhySocket *)0;',
        'if (!ZT_PHY_SOCKFD_VALID(s)) {\n#ifdef __3DS__\n            ctr_zt_trace(6, errno);\n#endif\n            return (PhySocket *)0;\n        }')
    s = diagnostic_replace(s, 'if (::bind(s,localAddress,(localAddress->sa_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in))) {',
        'if (::bind(s,localAddress,(localAddress->sa_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in))) {\n#ifdef __3DS__\n            ctr_zt_trace(6, errno);\n#endif')
    s = diagnostic_replace(s, 'sws.type = ZT_PHY_SOCKET_UDP;',
        '#ifdef __3DS__\n        ctr_zt_trace(2, 0);\n#endif\n        sws.type = ZT_PHY_SOCKET_UDP;')
    old = 'return ((long)::sendto(sws.sock,data,len,0,remoteAddress,(remoteAddress->sa_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in)) == (long)len);'
    s = diagnostic_replace(s, old, '#ifdef __3DS__\n        long result = (long)::sendto(sws.sock,data,len,0,remoteAddress,(remoteAddress->sa_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in));\n        if (result == (long)len) ctr_zt_trace(3, 0); else ctr_zt_trace(6, errno);\n        return result == (long)len;\n#else\n        ' + old + '\n#endif')
    s = diagnostic_replace(s, 'long n = (long)::recvfrom(s->sock,buf,sizeof(buf),0,(struct sockaddr *)&ss,&slen);',
        'long n = (long)::recvfrom(s->sock,buf,sizeof(buf),0,(struct sockaddr *)&ss,&slen);\n#ifdef __3DS__\n                            if (n > 0) ctr_zt_trace(4, 0);\n                            else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) ctr_zt_trace(6, errno);\n#endif')
    s = diagnostic_replace(s, 'if (::select((int)_nfds + 1,&rfds,&wfds,&efds,(timeout > 0) ? &tv : (struct timeval *)0) <= 0)\n\t\t\treturn;',
        'int selected = ::select((int)_nfds + 1,&rfds,&wfds,&efds,(timeout > 0) ? &tv : (struct timeval *)0);\n#ifdef __3DS__\n        ctr_zt_trace(5, 0);\n        if (selected < 0) ctr_zt_trace(6, errno);\n#endif\n        if (selected <= 0) return;')
    return s
edit('ext/ZeroTierOne/osdep/Phy.hpp', trace_phy)
