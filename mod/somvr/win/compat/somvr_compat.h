/* somvr_compat.h - the POSIX subset the game-side code uses, on Windows (win/README.md; forced into every Windows
 * TU with -include). The stand-in headers in this folder (pthread.h, unistd.h, sys/mman.h, ...) include only this.
 * Implementations: win/somvr_win_compat.c. Linux never sees this folder. */
#ifndef SOMVR_COMPAT_H
#define SOMVR_COMPAT_H
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _USE_MATH_DEFINES /* M_PI */
#include <errno.h>
#include <math.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

typedef intptr_t ssize_t;
typedef int pid_t;
typedef int clockid_t;
typedef unsigned long long sigset_t;
#define __attribute_visibility_ok 1

/* ---- time ------------------------------------------------------------------------------------------------- */
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#define CLOCK_BOOTTIME CLOCK_MONOTONIC
int clock_gettime(clockid_t c, struct timespec *ts);
int nanosleep(const struct timespec *req, struct timespec *rem);
int usleep(unsigned us);
unsigned sleep(unsigned s);

/* ---- threads ---------------------------------------------------------------------------------------------- */
typedef uintptr_t pthread_t;
typedef struct { void *p; } pthread_mutex_t; /* an SRWLOCK (exclusive) */
typedef struct { void *p; } pthread_cond_t;  /* a CONDITION_VARIABLE */
typedef struct { void *p; } pthread_once_t;  /* an INIT_ONCE */
typedef struct { int detached; size_t stack; } pthread_attr_t;
typedef int pthread_mutexattr_t;
typedef int pthread_condattr_t;
#define PTHREAD_MUTEX_INITIALIZER {0}
#define PTHREAD_COND_INITIALIZER {0}
#define PTHREAD_ONCE_INIT {0}
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_CREATE_JOINABLE 0
int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg);
int pthread_join(pthread_t t, void **ret);
int pthread_detach(pthread_t t);
pthread_t pthread_self(void);
int pthread_equal(pthread_t a, pthread_t b);
int pthread_attr_init(pthread_attr_t *a);
int pthread_attr_destroy(pthread_attr_t *a);
int pthread_attr_setdetachstate(pthread_attr_t *a, int d);
int pthread_attr_setstacksize(pthread_attr_t *a, size_t n);
int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a);
int pthread_mutex_destroy(pthread_mutex_t *m);
int pthread_mutex_lock(pthread_mutex_t *m);
int pthread_mutex_trylock(pthread_mutex_t *m);
int pthread_mutex_unlock(pthread_mutex_t *m);
int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a);
int pthread_cond_destroy(pthread_cond_t *c);
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m);
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abs_realtime);
int pthread_cond_signal(pthread_cond_t *c);
int pthread_cond_broadcast(pthread_cond_t *c);
int pthread_once(pthread_once_t *o, void (*fn)(void));
int pthread_setname_np(pthread_t t, const char *name);
int pthread_getname_np(pthread_t t, char *name, size_t n);
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old);
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
static inline int sigfillset(sigset_t *s) { *s = ~0ull; return 0; }
static inline int sigemptyset(sigset_t *s) { *s = 0; return 0; }
static inline int sigaddset(sigset_t *s, int n) { *s |= 1ull << (n & 63); return 0; }

/* gettid / prctl names / the thread's CPU time */
long syscall(long nr, ...);
#define SYS_gettid 186
#define SYS_process_vm_readv 310
#define SYS_process_vm_writev 311
#define SYS_perf_event_open 298
pid_t getpid(void);
pid_t gettid(void);
#define PR_SET_NAME 15
#define PR_GET_NAME 16
int prctl(int op, ...);
struct iovec { void *iov_base; size_t iov_len; };

/* ---- memory ----------------------------------------------------------------------------------------------- */
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FIXED_NOREPLACE 0x100000
#define MAP_FAILED ((void *)-1)
void *mmap(void *addr, size_t n, int prot, int flags, int fd, long off); /* anonymous only; FIXED_NOREPLACE = exactly
									     * there or MAP_FAILED */
int munmap(void *addr, size_t n);
int mprotect(void *addr, size_t n, int prot);
#define _SC_PAGESIZE 30
#define _SC_NPROCESSORS_ONLN 84
long sysconf(int name);
/* the protection of the page holding a (PROT_* bits), -1 = not mapped (VirtualQuery) */
int somvr_page_prot(uintptr_t a);
/* n bytes at a, read or written through ReadProcessMemory / WriteProcessMemory on ourselves: a bad address fails */
int somvr_safe_read(void *dst, uintptr_t a, size_t n);
int somvr_safe_write(uintptr_t a, const void *src, size_t n);
/* a page (n bytes, RW) within +-2 GB of [lo, hi] (the exe's code), NULL if none */
void *somvr_alloc_near(uintptr_t lo, uintptr_t hi, size_t n);

/* ---- files ------------------------------------------------------------------------------------------------ */
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif
#define F_OK 0
#define W_OK 2
#define R_OK 4
#define X_OK 0
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
ssize_t readlink(const char *path, char *buf, size_t n); /* "/proc/self/exe": the exe's path; else -1 */
int fsync(int fd);
#define strtok_r strtok_s
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
char *realpath(const char *path, char *out);
char *strndup(const char *s, size_t n);
char *strcasestr(const char *h, const char *n);
#define strtod_l _strtod_l
#define strtof_l _strtof_l
/* malloc's 16-byte alignment (free() must work on it): the one caller (the render fold's copy) needs no more */
static inline int posix_memalign(void **p, size_t align, size_t n) { (void)align; *p = malloc(n); return *p ? 0 : ENOMEM; }
static inline int pthread_atfork(void (*a)(void), void (*b)(void), void (*c)(void)) { (void)a, (void)b, (void)c; return 0; }


/* ---- locale: the C numeric locale for strtod/printf (newlocale/uselocale) ------------------------------- */
typedef _locale_t locale_t;
#define LC_ALL_MASK 0x7f
#define LC_NUMERIC_MASK 0x08
locale_t newlocale(int mask, const char *name, locale_t base);
locale_t uselocale(locale_t l);
void freelocale(locale_t l);

/* ---- dlfcn ------------------------------------------------------------------------------------------------ */
#define RTLD_DEFAULT ((void *)0)
#define RTLD_NOW 2
#define RTLD_LOCAL 0
#define RTLD_NOLOAD 4
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info;
void *dlopen(const char *name, int flags);
void *dlsym(void *h, const char *name);
int dlclose(void *h);
char *dlerror(void);
int dladdr(const void *a, Dl_info *info);

/* __builtin___clear_cache: FlushInstructionCache */
void somvr_flush_icache(void *a, size_t n);

/* ---- paths: the SOMVR_* variables hold Unix paths (Wine copies the Unix environment). Under Wine "/x/y" is opened
 * as "Z:\x\y" (Z: = the Unix root in every Proton prefix); on Windows, or a path with a drive, as given. ---- */
const char *somvr_path(const char *p, char *buf, size_t n);
int somvr_open(const char *p, int flags, ...);
FILE *somvr_fopen(const char *p, const char *mode);
int somvr_stat(const char *p, struct stat *st);
int somvr_access(const char *p, int mode);
int somvr_unlink(const char *p);
int somvr_rename(const char *a, const char *b);
int somvr_mkdir(const char *p, int mode);
#define open(...) somvr_open(__VA_ARGS__)
#define fopen(...) somvr_fopen(__VA_ARGS__)
#define stat(p, st) somvr_stat(p, st)
#define access(...) somvr_access(__VA_ARGS__)
#define unlink(...) somvr_unlink(__VA_ARGS__)
#define rename(...) somvr_rename(__VA_ARGS__)
#undef mkdir
#define mkdir(...) somvr_mkdir(__VA_ARGS__)
pid_t getppid(void);
int setenv(const char *k, const char *v, int over);
int unsetenv(const char *k);
static inline struct tm *localtime_r(const time_t *t, struct tm *out) { return localtime_s(out, t) ? NULL : out; }
static inline struct tm *gmtime_r(const time_t *t, struct tm *out) { return gmtime_s(out, t) ? NULL : out; }
/* 1 under Wine (ntdll exports wine_get_version) */
int somvr_under_wine(void);

#endif
