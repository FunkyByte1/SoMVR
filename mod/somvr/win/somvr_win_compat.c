/* somvr_win_compat.c - the POSIX subset of win/compat/somvr_compat.h on Win32 (and Wine). */
#include <windows.h>
#include <process.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <tlhelp32.h>

#undef open
#undef fopen
#undef stat
#undef access
#undef unlink
#undef rename
#undef mkdir

/* ---- time ------------------------------------------------------------------------------------------------- */
static LARGE_INTEGER qpf(void)
{
	static LARGE_INTEGER f;
	if (!f.QuadPart)
		QueryPerformanceFrequency(&f);
	return f;
}

static void ft_to_ts(ULONGLONG t100, struct timespec *ts)
{
	ts->tv_sec = (time_t)(t100 / 10000000ull);
	ts->tv_nsec = (long)(t100 % 10000000ull) * 100;
}

int clock_gettime(clockid_t c, struct timespec *ts)
{
	if (c == CLOCK_REALTIME) {
		FILETIME ft;
		GetSystemTimePreciseAsFileTime(&ft);
		ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32 | ft.dwLowDateTime) - 116444736000000000ull;
		ft_to_ts(t, ts);
		return 0;
	}
	if (c == CLOCK_THREAD_CPUTIME_ID || c == CLOCK_PROCESS_CPUTIME_ID) {
		FILETIME a, b, k, u;
		BOOL ok = c == CLOCK_THREAD_CPUTIME_ID ? GetThreadTimes(GetCurrentThread(), &a, &b, &k, &u)
						       : GetProcessTimes(GetCurrentProcess(), &a, &b, &k, &u);
		if (!ok)
			return errno = EINVAL, -1;
		ft_to_ts(((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) +
				 ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime), ts);
		return 0;
	}
	LARGE_INTEGER n, f = qpf();
	QueryPerformanceCounter(&n);
	ts->tv_sec = (time_t)(n.QuadPart / f.QuadPart);
	ts->tv_nsec = (long)((n.QuadPart % f.QuadPart) * 1000000000ll / f.QuadPart);
	return 0;
}

/* a waitable high-resolution timer per thread: Sleep() alone rounds to the 1-15.6 ms tick */
int nanosleep(const struct timespec *req, struct timespec *rem)
{
	static __declspec(thread) HANDLE tm;
	if (rem)
		rem->tv_sec = 0, rem->tv_nsec = 0;
	long long ns = (long long)req->tv_sec * 1000000000ll + req->tv_nsec;
	if (ns <= 0)
		return SwitchToThread(), 0;
	if (!tm) /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION = 2 (Windows 10 1803+; Wine: accepted) */
		tm = CreateWaitableTimerExW(NULL, NULL, 0x2, TIMER_ALL_ACCESS);
	if (!tm)
		tm = CreateWaitableTimerW(NULL, TRUE, NULL);
	LARGE_INTEGER due = { .QuadPart = -(ns / 100 > 0 ? ns / 100 : 1) };
	if (tm && SetWaitableTimer(tm, &due, 0, NULL, NULL, FALSE))
		WaitForSingleObject(tm, INFINITE);
	else
		Sleep((DWORD)((ns + 999999) / 1000000));
	return 0;
}

int usleep(unsigned us)
{
	struct timespec t = { (time_t)(us / 1000000u), (long)(us % 1000000u) * 1000 };
	return nanosleep(&t, NULL);
}

unsigned sleep(unsigned s)
{
	Sleep(s * 1000u);
	return 0;
}

/* ---- threads ---------------------------------------------------------------------------------------------- */
struct thr_start {
	void *(*fn)(void *);
	void *arg;
};

static unsigned __stdcall thr_main(void *p)
{
	struct thr_start s = *(struct thr_start *)p;
	free(p);
	s.fn(s.arg);
	return 0;
}

int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg)
{
	struct thr_start *s = malloc(sizeof *s);
	if (!s)
		return ENOMEM;
	s->fn = fn, s->arg = arg;
	unsigned id;
	uintptr_t h = _beginthreadex(NULL, a && a->stack ? (unsigned)a->stack : 0, thr_main, s, 0, &id);
	if (!h)
		return free(s), EAGAIN;
	if (a && a->detached) {
		CloseHandle((HANDLE)h);
		h = 0;
	}
	if (t)
		*t = h ? h : (uintptr_t)id | (uintptr_t)1 << 63; /* a detached thread: its id, never joined */
	return 0;
}

int pthread_join(pthread_t t, void **ret)
{
	if (ret)
		*ret = NULL;
	if (!t || t >> 63)
		return EINVAL;
	WaitForSingleObject((HANDLE)t, INFINITE);
	CloseHandle((HANDLE)t);
	return 0;
}

int pthread_detach(pthread_t t)
{
	if (t && !(t >> 63))
		CloseHandle((HANDLE)t);
	return 0;
}

pthread_t pthread_self(void) { return (pthread_t)GetCurrentThreadId() | (uintptr_t)1 << 62; }
int pthread_equal(pthread_t a, pthread_t b) { return a == b; }
int pthread_attr_init(pthread_attr_t *a) { a->detached = 0, a->stack = 0; return 0; }
int pthread_attr_destroy(pthread_attr_t *a) { (void)a; return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *a, int d) { a->detached = d == PTHREAD_CREATE_DETACHED; return 0; }
int pthread_attr_setstacksize(pthread_attr_t *a, size_t n) { a->stack = n; return 0; }

_Static_assert(sizeof(pthread_mutex_t) == sizeof(SRWLOCK), "mutex = SRWLOCK");
_Static_assert(sizeof(pthread_cond_t) == sizeof(CONDITION_VARIABLE), "cond = CONDITION_VARIABLE");
_Static_assert(sizeof(pthread_once_t) == sizeof(INIT_ONCE), "once = INIT_ONCE");
int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) { (void)a; InitializeSRWLock((PSRWLOCK)m); return 0; }
int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_lock(pthread_mutex_t *m) { AcquireSRWLockExclusive((PSRWLOCK)m); return 0; }
int pthread_mutex_trylock(pthread_mutex_t *m) { return TryAcquireSRWLockExclusive((PSRWLOCK)m) ? 0 : EBUSY; }
int pthread_mutex_unlock(pthread_mutex_t *m) { ReleaseSRWLockExclusive((PSRWLOCK)m); return 0; }
int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) { (void)a; InitializeConditionVariable((PCONDITION_VARIABLE)c); return 0; }
int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
	return SleepConditionVariableSRW((PCONDITION_VARIABLE)c, (PSRWLOCK)m, INFINITE, 0) ? 0 : EINVAL;
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abs)
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	long long ms = ((long long)abs->tv_sec - now.tv_sec) * 1000 + (abs->tv_nsec - now.tv_nsec) / 1000000;
	if (ms < 0)
		ms = 0;
	if (SleepConditionVariableSRW((PCONDITION_VARIABLE)c, (PSRWLOCK)m, (DWORD)ms, 0))
		return 0;
	return GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : EINVAL;
}

int pthread_cond_signal(pthread_cond_t *c) { WakeConditionVariable((PCONDITION_VARIABLE)c); return 0; }
int pthread_cond_broadcast(pthread_cond_t *c) { WakeAllConditionVariable((PCONDITION_VARIABLE)c); return 0; }

static BOOL CALLBACK once_cb(PINIT_ONCE o, PVOID p, PVOID *ctx)
{
	(void)o, (void)ctx;
	((void (*)(void))p)();
	return TRUE;
}

int pthread_once(pthread_once_t *o, void (*fn)(void))
{
	return InitOnceExecuteOnce((PINIT_ONCE)o, once_cb, (PVOID)fn, NULL) ? 0 : EINVAL;
}

/* SetThreadDescription (kernel32 on Windows 10 1607+; Wine has it) */
int pthread_setname_np(pthread_t t, const char *name)
{
	typedef HRESULT(WINAPI * set_t)(HANDLE, PCWSTR);
	static set_t set;
	if (!set)
		set = (set_t)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription");
	wchar_t w[64];
	MultiByteToWideChar(CP_UTF8, 0, name, -1, w, 64);
	w[63] = 0;
	HANDLE h = t >> 62 == 1 ? OpenThread(THREAD_SET_LIMITED_INFORMATION, FALSE, (DWORD)t) : (HANDLE)t;
	if (set && h)
		set(h, w);
	if (t >> 62 == 1 && h)
		CloseHandle(h);
	return 0;
}

int pthread_getname_np(pthread_t t, char *name, size_t n)
{
	(void)t;
	if (n)
		snprintf(name, n, "somvr");
	return 0;
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *old)
{
	(void)how, (void)set;
	if (old)
		*old = 0;
	return 0;
}

static __declspec(thread) char t_name[16];
int prctl(int op, ...)
{
	va_list ap;
	va_start(ap, op);
	char *p = va_arg(ap, char *);
	va_end(ap);
	if (op == PR_SET_NAME && p) {
		snprintf(t_name, sizeof t_name, "%s", p);
		pthread_setname_np(pthread_self(), p);
	} else if (op == PR_GET_NAME && p) {
		snprintf(p, 16, "%s", t_name[0] ? t_name : "game");
	}
	return 0;
}

pid_t getpid(void) { return (pid_t)GetCurrentProcessId(); }
pid_t gettid(void) { return (pid_t)GetCurrentThreadId(); }

pid_t getppid(void)
{
	DWORD me = GetCurrentProcessId(), pp = 0;
	HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 e = { .dwSize = sizeof e };
	if (s != INVALID_HANDLE_VALUE) {
		for (BOOL ok = Process32First(s, &e); ok; ok = Process32Next(s, &e))
			if (e.th32ProcessID == me) {
				pp = e.th32ParentProcessID;
				break;
			}
		CloseHandle(s);
	}
	return (pid_t)pp;
}

/* ---- memory ----------------------------------------------------------------------------------------------- */
static DWORD prot_win(int p)
{
	if (p & PROT_EXEC)
		return p & PROT_WRITE ? PAGE_EXECUTE_READWRITE : p & PROT_READ ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
	return p & PROT_WRITE ? PAGE_READWRITE : p & PROT_READ ? PAGE_READONLY : PAGE_NOACCESS;
}

static int prot_posix(DWORD p)
{
	switch (p & 0xff) {
	case PAGE_READONLY: return PROT_READ;
	case PAGE_READWRITE: case PAGE_WRITECOPY: return PROT_READ | PROT_WRITE;
	case PAGE_EXECUTE: return PROT_EXEC;
	case PAGE_EXECUTE_READ: return PROT_READ | PROT_EXEC;
	case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY: return PROT_READ | PROT_WRITE | PROT_EXEC;
	default: return 0;
	}
}

void *mmap(void *addr, size_t n, int prot, int flags, int fd, long off)
{
	(void)off;
	if (fd != -1 || !(flags & MAP_ANONYMOUS))
		return errno = ENODEV, MAP_FAILED;
	void *p = VirtualAlloc(addr, n, MEM_RESERVE | MEM_COMMIT, prot_win(prot));
	if (!p && addr && !(flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)))
		p = VirtualAlloc(NULL, n, MEM_RESERVE | MEM_COMMIT, prot_win(prot));
	if (!p || (addr && (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) && p != addr)) {
		if (p)
			VirtualFree(p, 0, MEM_RELEASE);
		return errno = EEXIST, MAP_FAILED;
	}
	return p;
}

int munmap(void *addr, size_t n)
{
	(void)n;
	return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : (errno = EINVAL, -1);
}

int mprotect(void *addr, size_t n, int prot)
{
	DWORD old;
	return VirtualProtect(addr, n, prot_win(prot), &old) ? 0 : (errno = EACCES, -1);
}

long sysconf(int name)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return name == _SC_PAGESIZE ? (long)si.dwPageSize : name == _SC_NPROCESSORS_ONLN ? (long)si.dwNumberOfProcessors : -1;
}

int somvr_page_prot(uintptr_t a)
{
	MEMORY_BASIC_INFORMATION m;
	if (!VirtualQuery((void *)a, &m, sizeof m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
		return -1;
	return prot_posix(m.Protect);
}

/* safe reads: a small cache of committed readable regions (VirtualQuery is a lock + a tree walk under Wine, a
 * syscall on Windows), then an SEH-guarded copy for the rare region freed since it was cached */
#define RC_N 64
static struct { _Atomic uintptr_t lo, hi; } g_rc[RC_N];
static _Atomic unsigned g_rc_next;
static _Atomic uint64_t g_rc_gen; /* bumped by somvr_safe_forget (a fault): the cache restarts */

static int region_readable(uintptr_t a, size_t n)
{
	for (int i = 0; i < RC_N; i++) {
		uintptr_t lo = atomic_load_explicit(&g_rc[i].lo, memory_order_relaxed),
			  hi = atomic_load_explicit(&g_rc[i].hi, memory_order_relaxed);
		if (a >= lo && a + n <= hi && a + n >= a)
			return 1;
	}
	uintptr_t p = a, end = a + n;
	while (p < end) {
		MEMORY_BASIC_INFORMATION m;
		if (!VirtualQuery((void *)p, &m, sizeof m) || m.State != MEM_COMMIT ||
		    (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(prot_posix(m.Protect) & PROT_READ))
			return 0;
		uintptr_t lo = (uintptr_t)m.BaseAddress, hi = lo + m.RegionSize;
		unsigned k = atomic_fetch_add_explicit(&g_rc_next, 1, memory_order_relaxed) % RC_N;
		atomic_store_explicit(&g_rc[k].lo, 0, memory_order_relaxed); /* never a torn [lo, hi) */
		atomic_store_explicit(&g_rc[k].hi, hi, memory_order_relaxed);
		atomic_store_explicit(&g_rc[k].lo, lo, memory_order_relaxed);
		p = hi;
	}
	return 1;
}

static void rc_forget(void)
{
	for (int i = 0; i < RC_N; i++)
		atomic_store_explicit(&g_rc[i].hi, 0, memory_order_relaxed);
	atomic_fetch_add(&g_rc_gen, 1);
}

static int guarded_copy(void *dst, const void *src, size_t n)
{
	int ok = 1;
	__try {
		memcpy(dst, src, n);
	} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
								      : EXCEPTION_CONTINUE_SEARCH) {
		ok = 0;
	}
	return ok;
}

int somvr_safe_read(void *dst, uintptr_t a, size_t n)
{
	if (!a || a + n < a || a < 0x10000)
		return 0;
	if (!region_readable(a, n))
		return 0;
	if (guarded_copy(dst, (const void *)a, n))
		return 1;
	rc_forget();
	return 0;
}

int somvr_safe_write(uintptr_t a, const void *src, size_t n)
{
	if (!a || a + n < a || a < 0x10000)
		return 0;
	for (uintptr_t p = a & ~(uintptr_t)4095; p < a + n; p += 4096)
		if (!(somvr_page_prot(p) & PROT_WRITE) || somvr_page_prot(p) < 0)
			return 0;
	if (guarded_copy((void *)a, src, n))
		return 1;
	rc_forget();
	return 0;
}

/* syscall(): the three numbers the game-side code uses */
long syscall(long nr, ...)
{
	va_list ap;
	va_start(ap, nr);
	long r = -1;
	if (nr == SYS_gettid) {
		r = (long)GetCurrentThreadId();
	} else if (nr == SYS_process_vm_readv || nr == SYS_process_vm_writev) {
		(void)va_arg(ap, long);                                  /* pid: always ourselves */
		const struct iovec *l = va_arg(ap, const struct iovec *); /* local */
		unsigned long nl = va_arg(ap, unsigned long);
		const struct iovec *rm = va_arg(ap, const struct iovec *); /* remote */
		unsigned long nr2 = va_arg(ap, unsigned long);
		if (nl == 1 && nr2 == 1 && l->iov_len == rm->iov_len) {
			int ok = nr == SYS_process_vm_readv ? somvr_safe_read(l->iov_base, (uintptr_t)rm->iov_base, l->iov_len)
							    : somvr_safe_write((uintptr_t)rm->iov_base, l->iov_base, l->iov_len);
			r = ok ? (long)l->iov_len : -1;
		}
		if (r < 0)
			errno = EFAULT;
	} else {
		errno = ENOSYS;
	}
	va_end(ap);
	return r;
}

void *somvr_alloc_near(uintptr_t lo, uintptr_t hi, size_t n)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	uintptr_t g = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
	n = (n + 4095) & ~(uintptr_t)4095;
	/* below lo first (free space under the image), then above hi; every candidate within +-2 GB of both ends */
	for (int dir = 0; dir < 2; dir++)
		for (uintptr_t k = 1; k < 0x7f00; k++) {
			uintptr_t at = dir == 0 ? ((lo & ~(g - 1)) - k * g) : (((hi + g - 1) & ~(g - 1)) + k * g);
			if ((dir == 0 && at > lo) || at < 0x10000)
				break;
			int64_t d1 = (int64_t)at - (int64_t)lo, d2 = (int64_t)(at + n) - (int64_t)hi;
			if (d1 < -0x7ff00000ll || d1 > 0x7ff00000ll || d2 < -0x7ff00000ll || d2 > 0x7ff00000ll)
				break;
			MEMORY_BASIC_INFORMATION m;
			if (!VirtualQuery((void *)at, &m, sizeof m) || m.State != MEM_FREE)
				continue;
			void *p = VirtualAlloc((void *)at, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
			if (p)
				return p;
		}
	return NULL;
}

void somvr_flush_icache(void *a, size_t n) { FlushInstructionCache(GetCurrentProcess(), a, n); }

/* ---- files and paths -------------------------------------------------------------------------------------- */
int somvr_under_wine(void)
{
	static int w = -1;
	if (w < 0)
		w = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != NULL;
	return w;
}

const char *somvr_path(const char *p, char *buf, size_t n)
{
	if (!p || p[0] != '/' || p[1] == '/' || !somvr_under_wine())
		return p;
	if (snprintf(buf, n, "Z:%s", p) >= (int)n)
		return p;
	for (char *q = buf; *q; q++)
		if (*q == '/')
			*q = '\\';
	return buf;
}

int somvr_open(const char *p, int flags, ...)
{
	char b[1024];
	int mode = 0;
	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	}
	return _open(somvr_path(p, b, sizeof b), flags | O_BINARY | _O_NOINHERIT, mode ? _S_IREAD | _S_IWRITE : 0);
}

FILE *somvr_fopen(const char *p, const char *mode)
{
	char b[1024], m[16];
	size_t k = 0; /* "re": glibc's close-on-exec flag; the UCRT's "N" (not inherited) */
	for (const char *q = mode; *q && k + 2 < sizeof m; q++)
		m[k++] = *q == 'e' ? 'N' : *q;
	m[k] = 0;
	return fopen(somvr_path(p, b, sizeof b), m);
}

int somvr_stat(const char *p, struct stat *st)
{
	char b[1024];
	return stat(somvr_path(p, b, sizeof b), st);
}

int somvr_access(const char *p, int mode)
{
	char b[1024];
	return _access(somvr_path(p, b, sizeof b), mode & 6);
}

int somvr_unlink(const char *p)
{
	char b[1024];
	return _unlink(somvr_path(p, b, sizeof b));
}

int somvr_rename(const char *a, const char *bb)
{
	char x[1024], y[1024];
	const char *pa = somvr_path(a, x, sizeof x), *pb = somvr_path(bb, y, sizeof y);
	return MoveFileExA(pa, pb, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : (errno = EACCES, -1);
}

int somvr_mkdir(const char *p, int mode)
{
	(void)mode;
	char b[1024];
	return _mkdir(somvr_path(p, b, sizeof b));
}

ssize_t readlink(const char *path, char *buf, size_t n)
{
	if (strcmp(path, "/proc/self/exe") || !n)
		return errno = EINVAL, -1;
	DWORD k = GetModuleFileNameA(NULL, buf, (DWORD)n);
	return k && k < n ? (ssize_t)k : (errno = ENAMETOOLONG, -1);
}

int fsync(int fd) { return _commit(fd); }

char *realpath(const char *path, char *out)
{
	char b[1024];
	return _fullpath(out, somvr_path(path, b, sizeof b), out ? 1024 : 0);
}

char *strndup(const char *s, size_t n)
{
	size_t k = strnlen(s, n);
	char *r = malloc(k + 1);
	if (r)
		memcpy(r, s, k), r[k] = 0;
	return r;
}

char *strcasestr(const char *h, const char *n)
{
	size_t k = strlen(n);
	for (; *h; h++)
		if (!_strnicmp(h, n, k))
			return (char *)h;
	return k ? NULL : (char *)h;
}

int setenv(const char *k, const char *v, int over)
{
	if (!over && getenv(k))
		return 0;
	return _putenv_s(k, v) ? -1 : 0;
}

int unsetenv(const char *k) { return _putenv_s(k, "") ? -1 : 0; }

/* ---- locale ----------------------------------------------------------------------------------------------- */
static __declspec(thread) locale_t t_loc;
locale_t newlocale(int mask, const char *name, locale_t base)
{
	(void)mask, (void)base;
	return _create_locale(LC_ALL, name);
}

/* the UCRT has no per-thread locale switch for plain strtod/printf: _configthreadlocale + setlocale on this thread */
locale_t uselocale(locale_t l)
{
	locale_t old = t_loc;
	if (l) {
		_configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
		setlocale(LC_NUMERIC, "C");
		t_loc = l;
	}
	return old;
}

void freelocale(locale_t l) { _free_locale(l); }

/* ---- dlfcn: GetProcAddress over the DLL itself and the exe ---------------------------------------------- */
static char g_dlerr[128];
void *dlopen(const char *name, int flags)
{
	HMODULE h = name ? ((flags & RTLD_NOLOAD) ? GetModuleHandleA(name) : LoadLibraryA(name)) : GetModuleHandleA(NULL);
	if (!h)
		snprintf(g_dlerr, sizeof g_dlerr, "LoadLibrary(%s) failed: %lu", name ? name : "", GetLastError());
	return h;
}

void *dlsym(void *h, const char *name)
{
	if (h)
		return (void *)GetProcAddress((HMODULE)h, name);
	HMODULE me = NULL;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			   (LPCSTR)(void *)dlsym, &me);
	void *p = me ? (void *)GetProcAddress(me, name) : NULL;
	return p ? p : (void *)GetProcAddress(GetModuleHandleA(NULL), name);
}

int dlclose(void *h) { return h && FreeLibrary((HMODULE)h) ? 0 : -1; }
char *dlerror(void) { return g_dlerr[0] ? g_dlerr : NULL; }

int dladdr(const void *a, Dl_info *info)
{
	static char name[MAX_PATH];
	HMODULE m = NULL;
	memset(info, 0, sizeof *info);
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCSTR)a, &m))
		return 0;
	GetModuleFileNameA(m, name, sizeof name);
	info->dli_fname = name, info->dli_fbase = m;
	return 1;
}
