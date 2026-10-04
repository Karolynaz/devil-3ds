#pragma once
/* Private adapter for the pinned ZeroTier/lwIP implementation. */
#include <pthread.h>
#include <3ds.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

typedef Thread ctr_zt_thread_t;
typedef size_t ctr_zt_attr_t;
typedef int ctr_zt_mutexattr_t;
typedef int ctr_zt_condattr_t;
typedef CondVar ctr_zt_cond_t;
typedef struct { LightLock gate; u32 owner; unsigned depth; int recursive; } ctr_zt_mutex_t;
#undef PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_MUTEX_INITIALIZER { 1, 0, 0, 0 }
#define pthread_t ctr_zt_thread_t
#define pthread_attr_t ctr_zt_attr_t
#define pthread_mutex_t ctr_zt_mutex_t
#define pthread_mutexattr_t ctr_zt_mutexattr_t
#define pthread_cond_t ctr_zt_cond_t
#define pthread_condattr_t ctr_zt_condattr_t

static inline u32 ctr_zt_thread_id(void) { u32 id = 0; svcGetThreadId(&id, CUR_THREAD_HANDLE); return id; }
static inline int ctr_zt_mutex_init(ctr_zt_mutex_t *m, const ctr_zt_mutexattr_t *a) { LightLock_Init(&m->gate); m->owner = m->depth = 0; m->recursive = a && *a == PTHREAD_MUTEX_RECURSIVE; return 0; }
static inline int ctr_zt_mutex_lock(ctr_zt_mutex_t *m) { u32 id = ctr_zt_thread_id(); if (m->recursive && __atomic_load_n(&m->owner, __ATOMIC_ACQUIRE) == id) { ++m->depth; return 0; } LightLock_Lock(&m->gate); m->depth = 1; __atomic_store_n(&m->owner, id, __ATOMIC_RELEASE); return 0; }
static inline int ctr_zt_mutex_unlock(ctr_zt_mutex_t *m) { if (--m->depth == 0) { __atomic_store_n(&m->owner, 0, __ATOMIC_RELEASE); LightLock_Unlock(&m->gate); } return 0; }
static inline int ctr_zt_mutex_destroy(ctr_zt_mutex_t *m) { (void)m; return 0; }
static inline int ctr_zt_mutexattr_init(ctr_zt_mutexattr_t *a) { *a = 0; return 0; }
static inline int ctr_zt_mutexattr_settype(ctr_zt_mutexattr_t *a, int type) { *a = type; return 0; }
static inline int ctr_zt_cond_init(CondVar *c, const ctr_zt_condattr_t *a) { (void)a; CondVar_Init(c); return 0; }
static inline int ctr_zt_cond_destroy(CondVar *c) { (void)c; return 0; }
static inline int ctr_zt_condattr_init(ctr_zt_condattr_t *a) { *a = 0; return 0; }
static inline int ctr_zt_condattr_destroy(ctr_zt_condattr_t *a) { (void)a; return 0; }
static inline int ctr_zt_condattr_setclock(ctr_zt_condattr_t *a, clockid_t c) { (void)a; (void)c; return 0; }
static inline void ctr_zt_monotonic(struct timespec *t) { u64 ticks = svcGetSystemTick(); t->tv_sec = ticks / SYSCLOCK_ARM11; t->tv_nsec = (ticks % SYSCLOCK_ARM11) * 1000000000ULL / SYSCLOCK_ARM11; }
static inline int ctr_zt_cond_wait(CondVar *c, ctr_zt_mutex_t *m) { m->owner = m->depth = 0; CondVar_Wait(c, &m->gate); m->owner = ctr_zt_thread_id(); m->depth = 1; return 0; }
static inline int ctr_zt_cond_timedwait(CondVar *c, ctr_zt_mutex_t *m, const struct timespec *deadline) { struct timespec now; ctr_zt_monotonic(&now); s64 ns = (s64)(deadline->tv_sec - now.tv_sec) * 1000000000LL + deadline->tv_nsec - now.tv_nsec; if (ns <= 0) return ETIMEDOUT; m->owner = m->depth = 0; int r = CondVar_WaitTimeout(c, &m->gate, ns); m->owner = ctr_zt_thread_id(); m->depth = 1; return r ? ETIMEDOUT : 0; }
static inline int ctr_zt_cond_broadcast(CondVar *c) { CondVar_Broadcast(c); return 0; }
struct ctr_zt_start { void *(*fn)(void *); void *arg; };
static inline void ctr_zt_trampoline(void *opaque) { struct ctr_zt_start start = *(struct ctr_zt_start *)opaque; free(opaque); start.fn(start.arg); }
static inline int ctr_zt_create(Thread *t, const ctr_zt_attr_t *attr, void *(*fn)(void *), void *arg) { struct ctr_zt_start *start = (struct ctr_zt_start *)malloc(sizeof(*start)); if (!start) return ENOMEM; start->fn = fn; start->arg = arg; *t = threadCreate(ctr_zt_trampoline, start, attr ? *attr : 256 * 1024, 0x30, -2, attr == NULL); if (!*t) { free(start); return EAGAIN; } return 0; }
static inline int ctr_zt_join(Thread t, void **value) { (void)value; if (t) { threadJoin(t, UINT64_MAX); threadFree(t); } return 0; }
static inline int ctr_zt_attr_init(ctr_zt_attr_t *a) { *a = 256 * 1024; return 0; }
static inline int ctr_zt_attr_setstacksize(ctr_zt_attr_t *a, size_t size) { *a = size; return 0; }
static inline int ctr_zt_attr_destroy(ctr_zt_attr_t *a) { (void)a; return 0; }
#define pthread_mutex_init ctr_zt_mutex_init
#define pthread_mutex_destroy ctr_zt_mutex_destroy
#define pthread_mutex_lock ctr_zt_mutex_lock
#define pthread_mutex_unlock ctr_zt_mutex_unlock
#define pthread_mutexattr_init ctr_zt_mutexattr_init
#define pthread_mutexattr_settype ctr_zt_mutexattr_settype
#define pthread_cond_init ctr_zt_cond_init
#define pthread_cond_destroy ctr_zt_cond_destroy
#define pthread_condattr_init ctr_zt_condattr_init
#define pthread_condattr_destroy ctr_zt_condattr_destroy
#define pthread_condattr_setclock ctr_zt_condattr_setclock
#define pthread_cond_wait ctr_zt_cond_wait
#define pthread_cond_timedwait ctr_zt_cond_timedwait
#define pthread_cond_broadcast ctr_zt_cond_broadcast
#define pthread_create ctr_zt_create
#define pthread_join ctr_zt_join
#define pthread_attr_init ctr_zt_attr_init
#define pthread_attr_setstacksize ctr_zt_attr_setstacksize
#define pthread_attr_destroy ctr_zt_attr_destroy
#define pthread_self threadGetCurrent
#define pthread_exit(value) threadExit(0)
