/* win_hcount.c - the soak's handle count on the Windows build (som-b33-soak, docs/re-winport.md 7.13): run in OUR
 * prefix beside the game (the same wineserver), it prints one line for every process whose exe name matches argv[1]
 * (default ShadowOfMordor.exe):
 *   HCOUNT pid=N threads=T handles=H (class C) types=NAME:COUNT,... all=TOTAL
 * The handles come from NtQuerySystemInformation's system handle list (SystemExtendedHandleInformation, then
 * SystemHandleInformation: Wine serves both from the wineserver's tables), counted per owner pid; the threads from a
 * toolhelp snapshot; the type names from NtQueryObject's ObjectTypesInformation (the index when a name is missing). A Windows-side handle leak (events, threads, files, sections never closed) grows `handles`; the
 * types say which kind. Reads only; no handle of its own is opened on the game.
 * Build: clang --target=x86_64-pc-windows-msvc (the xwin SDK in shared/toolchain) + lld-link, see scripts/soak33-drive.sh. */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef LONG(NTAPI *qsi_t)(ULONG, PVOID, ULONG, PULONG);
typedef LONG(NTAPI *qo_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef struct {
	USHORT Length, MaximumLength;
	PWSTR Buffer;
} ustr;
typedef struct { /* OBJECT_TYPE_INFORMATION (winternl's full layout), the name's buffer after it */
	ustr TypeName;
	ULONG TotalNumberOfObjects, TotalNumberOfHandles, TotalPagedPoolUsage, TotalNonPagedPoolUsage;
	ULONG TotalNamePoolUsage, TotalHandleTableUsage, HighWaterNumberOfObjects, HighWaterNumberOfHandles;
	ULONG HighWaterPagedPoolUsage, HighWaterNonPagedPoolUsage, HighWaterNamePoolUsage, HighWaterHandleTableUsage;
	ULONG InvalidAttributes;
	GENERIC_MAPPING GenericMapping;
	ULONG ValidAccessMask;
	BOOLEAN SecurityRequired, MaintainHandleCount;
	UCHAR TypeIndex;
	CHAR ReservedByte;
	ULONG PoolType, DefaultPagedPoolCharge, DefaultNonPagedPoolCharge;
} otype;
static char tname[256][24];
static int tshift; /* the handle list's type index + tshift = the type list's index (Wine: 2; Windows: 0) */
static const char *tn(int k) { return k + tshift >= 0 && k + tshift < 256 ? tname[k + tshift] : ""; }

/* the object types' names by index (NtQueryObject ObjectTypesInformation = 3): "Event", "File", ... */
static void type_names(void)
{
	qo_t qo = (qo_t)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryObject");
	ULONG sz = 1 << 16, need = 0;
	char *b = qo ? malloc(sz) : NULL;
	if (!b || qo(NULL, 3, b, sz, &need) < 0) {
		free(b);
		return;
	}
	ULONG nt = *(ULONG *)b;
	char *p = b + sizeof(void *);
	for (ULONG i = 0; i < nt && p < b + sz; i++) {
		otype *t = (otype *)p;
		int idx = t->TypeIndex ? t->TypeIndex : (int)i + 2;
		if (idx < 256)
			WideCharToMultiByte(CP_UTF8, 0, t->TypeName.Buffer, t->TypeName.Length / 2, tname[idx], 23, NULL, NULL);
		p += sizeof *t + ((t->TypeName.MaximumLength + sizeof(void *) - 1) & ~(sizeof(void *) - 1));
	}
	free(b);
}

typedef struct {
	PVOID Object;
	ULONG_PTR UniqueProcessId;
	ULONG_PTR HandleValue;
	ULONG GrantedAccess;
	USHORT CreatorBackTraceIndex;
	USHORT ObjectTypeIndex;
	ULONG HandleAttributes;
	ULONG Reserved;
} hx_entry;
typedef struct {
	ULONG_PTR NumberOfHandles;
	ULONG_PTR Reserved;
	hx_entry Handles[1];
} hx_info;
typedef struct {
	ULONG OwnerPid;
	BYTE ObjectType;
	BYTE HandleFlags;
	USHORT HandleValue;
	PVOID ObjectPointer;
	ULONG AccessMask;
} h_entry;
typedef struct {
	ULONG Count;
	h_entry Handle[1];
} h_info;

static void *query(qsi_t q, ULONG cls)
{
	ULONG sz = 1 << 20, need = 0;
	for (int i = 0; i < 8; i++) {
		void *b = malloc(sz);
		if (!b)
			return NULL;
		LONG st = q(cls, b, sz, &need);
		if (st >= 0)
			return b;
		free(b);
		if (st != (LONG)0xC0000004L) /* STATUS_INFO_LENGTH_MISMATCH */
			return NULL;
		sz = need > sz ? need + (1 << 16) : sz * 2;
	}
	return NULL;
}

/* --names TYPE [EXE]: the names of EXE's handles of TYPE ("Key", "File", "Event", ...), counted - each handle duplicated
 * into this process (DuplicateHandle: a copy of ours, the game's own handle untouched), its name read
 * (NtQueryObject ObjectNameInformation = 1), the copy closed; the 25 most frequent names */
typedef struct {
	char n[200];
	unsigned c;
} nm_t;
static int nm_cmp(const void *a, const void *b) { return (int)((const nm_t *)b)->c - (int)((const nm_t *)a)->c; }
static void names(qsi_t q, DWORD pid, const char *type)
{
	qo_t qo = (qo_t)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryObject");
	int want = -1;
	for (int t = 0; t < 250; t++)
		if (!_stricmp(tn(t), type))
			want = t;
	hx_info *x = query(q, 64);
	HANDLE gp = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid);
	if (want < 0 || !x || !gp || !qo) {
		printf("NAMES %s: no type index / handle list / process handle\n", type);
		return;
	}
	static nm_t nm[4096];
	int nn = 0;
	unsigned tot = 0, unnamed = 0;
	for (ULONG_PTR i = 0; i < x->NumberOfHandles; i++) {
		if (x->Handles[i].UniqueProcessId != pid || (x->Handles[i].ObjectTypeIndex & 0xff) != want)
			continue;
		tot++;
		HANDLE d = NULL;
		if (!DuplicateHandle(gp, (HANDLE)x->Handles[i].HandleValue, GetCurrentProcess(), &d, 0, FALSE, DUPLICATE_SAME_ACCESS))
			continue;
		union {
			ustr u;
			char b[1024];
		} buf;
		ULONG need = 0;
		char nb[200] = "";
		if (qo(d, 1, &buf, sizeof buf, &need) >= 0 && buf.u.Buffer && buf.u.Length)
			WideCharToMultiByte(CP_UTF8, 0, buf.u.Buffer, buf.u.Length / 2, nb, sizeof nb - 1, NULL, NULL);
		CloseHandle(d);
		if (!nb[0]) {
			unnamed++;
			continue;
		}
		int j = 0;
		while (j < nn && strcmp(nm[j].n, nb))
			j++;
		if (j == nn && nn < 4096)
			snprintf(nm[nn++].n, sizeof nm[0].n, "%s", nb);
		if (j < nn)
			nm[j].c++;
	}
	qsort(nm, nn, sizeof nm[0], nm_cmp);
	printf("NAMES pid=%lu %s: %u handles, %u unnamed, %d names\n", pid, type, tot, unnamed, nn);
	for (int j = 0; j < nn && j < 25; j++)
		printf("  %4u %s\n", nm[j].c, nm[j].n);
	CloseHandle(gp);
	free(x);
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "ShadowOfMordor.exe";
	const char *ntype = NULL;
	if (!strcmp(name, "--names") && argc > 2) {
		ntype = argv[2];
		name = argc > 3 ? argv[3] : "ShadowOfMordor.exe";
	}
	if (!strcmp(name, "--calibrate")) { /* known counts of our own: events 7, mutants 5, semaphores 3, files 11,
					      threads 2 (suspended), keys 4 - the type indices' names checked */
		for (int i = 0; i < 7; i++)
			CreateEventA(NULL, TRUE, FALSE, NULL);
		for (int i = 0; i < 5; i++)
			CreateMutexA(NULL, FALSE, NULL);
		for (int i = 0; i < 3; i++)
			CreateSemaphoreA(NULL, 0, 1, NULL);
		char me[MAX_PATH];
		GetModuleFileNameA(NULL, me, MAX_PATH);
		for (int i = 0; i < 11; i++)
			CreateFileA(me, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		for (int i = 0; i < 2; i++)
			CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(void *)Sleep, NULL, CREATE_SUSPENDED, NULL);
		HKEY k;
		for (int i = 0; i < 4; i++)
			RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software", 0, KEY_READ, &k);
		name = "win_hcount.exe";
		printf("calibrate: events 7, mutants 5, semaphores 3, files 11, threads 2, keys 4 made\n");
	}
	DWORD pids[16], thr[16];
	int n = 0;
	HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (s != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32 pe;
		pe.dwSize = sizeof pe;
		for (BOOL ok = Process32First(s, &pe); ok && n < 16; ok = Process32Next(s, &pe))
			if (!_stricmp(pe.szExeFile, name)) {
				pids[n] = pe.th32ProcessID;
				thr[n++] = pe.cntThreads;
			}
		CloseHandle(s);
	}
	if (!n) {
		printf("HCOUNT none: no process %s\n", name);
		return 1;
	}
	type_names();
	HANDLE probe = CreateEventA(NULL, TRUE, FALSE, NULL); /* a known object of ours: the indices' offset */
	qsi_t q = (qsi_t)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation");
	if (!q) {
		printf("HCOUNT no NtQuerySystemInformation\n");
		return 2;
	}
	{
		hx_info *x = query(q, 64);
		int ev = -1;
		for (ULONG_PTR i = 0; x && i < x->NumberOfHandles; i++)
			if (x->Handles[i].UniqueProcessId == GetCurrentProcessId() && x->Handles[i].HandleValue == (ULONG_PTR)probe)
				ev = x->Handles[i].ObjectTypeIndex & 0xff;
		for (int t = 0; ev >= 0 && t < 256; t++)
			if (!_stricmp(tname[t], "Event"))
				tshift = t - ev;
		free(x);
		CloseHandle(probe);
	}
	if (ntype) {
		for (int k = 0; k < n; k++)
			names(q, pids[k], ntype);
		return 0;
	}
	ULONG cls = 64;
	void *b = query(q, cls);
	if (!b) {
		cls = 16;
		b = query(q, cls);
	}
	for (int k = 0; k < n; k++) {
		unsigned long cnt = 0, all = 0, types[256] = {0};
		if (b && cls == 64) {
			hx_info *x = b;
			all = (unsigned long)x->NumberOfHandles;
			for (ULONG_PTR i = 0; i < x->NumberOfHandles; i++)
				if (x->Handles[i].UniqueProcessId == pids[k]) {
					cnt++;
					types[x->Handles[i].ObjectTypeIndex & 0xff]++;
				}
		} else if (b) {
			h_info *x = b;
			all = x->Count;
			for (ULONG i = 0; i < x->Count; i++)
				if (x->Handle[i].OwnerPid == pids[k]) {
					cnt++;
					types[x->Handle[i].ObjectType]++;
				}
		}
		char ts[1024];
		int o = 0;
		ts[0] = 0;
		for (int t = 0; t < 256 && o < (int)sizeof ts - 24; t++)
			if (types[t])
				o += tn(t)[0] ? snprintf(ts + o, sizeof ts - o, "%s%s:%lu", o ? "," : "", tn(t), types[t])
						  : snprintf(ts + o, sizeof ts - o, "%s%d:%lu", o ? "," : "", t, types[t]);
		if (b)
			printf("HCOUNT pid=%lu threads=%lu handles=%lu (class %lu) types=%s all=%lu\n", pids[k], thr[k], cnt, cls,
			       ts, all);
		else
			printf("HCOUNT pid=%lu threads=%lu handles=? (no handle list) types= all=?\n", pids[k], thr[k]);
	}
	free(b);
	return 0;
}
