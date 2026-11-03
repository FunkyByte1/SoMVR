/* somvr_win_d3d.c - the DLL's own D3D11/DXGI hooks (docs/re-winport.md 3.1, 7.1): the runtime's COM objects for the
 * COM sites, DXVK's interop for VkImages, the present pacing.
 *
 * The exe imports d3d11!D3D11CreateDevice and dxgi!CreateDXGIFactory (the game and the stand-in d3dtest alike): their
 * IAT entries are redirected in DllMain. D3D11CreateDevice: the device and the immediate context (their vtables fill
 * the ID3D11Device / ID3D11DeviceContext COM rows). CreateDXGIFactory: the factory's CreateSwapChain (slot 10) is
 * hooked, the new swap chain's Present (slot 8) too; the first Present fills the IDXGISwapChain and ID3D11Texture2D
 * rows (the back buffer's vtable), logs the back buffer's VkImage (IDXGIVkInteropSurface::GetVulkanImageInfo) and
 * calls winport-core's somvr_core_win_com(). Every Present: the interval statistics (a WIN PRESENT line per 10 s).
 *
 * SOMVR_WIN_TEST=1 (the stand-in): two "eye" textures of the back buffer's size are made after
 * somvr_shared.eyes.expect_w/h announce them (the layer remembers VkImages of that size), the back buffer is copied
 * into both every Present, their VkImages are published in somvr_shared.eyes (as H8 does) - the layer's log must
 * name the same handles (VkImages reach the layer unwrapped) - and a thread asks the PE locate stand-ins as the game
 * thread would (somvr_views_locate every 11 ms, the grips through somvr_input's pointer): WIN XRLOC lines. */
#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan_core.h>
#include <openxr/openxr.h>
#include "../somvr_shared.h"
#include "../somvr_input.h"
#include "../somvr_views.h"
#include "../somvr_xrloc.h"
#include "somvr_win.h"
#include "somvr_fogdxbc.h" /* winport-render: the fog by the distance, in DXBC */

extern somvr_shared g_sh_obj __asm__(SOMVR_SHARED_SYMBOL);
extern somvr_input_shared g_in_obj __asm__(SOMVR_INPUT_SYMBOL);
extern somvr_xrloc g_xrloc_obj __asm__(SOMVR_XRLOC_SYMBOL);
void somvr_core_win_com(void); /* somvr.c (winport-core): H8's vtables from the D3D_* COM rows */
void somvr_win_late_h9(void); /* somvr.c: H9 on the filled IDXGISwapChain row */

/* DXVK's interop interface (dxgi_interfaces.h): IUnknown, GetDevice, GetVulkanImageInfo */
static const GUID IID_VkInteropSurface = { 0x5546cf8c, 0x77e7, 0x4341, { 0xb0, 0x5d, 0x8d, 0x4d, 0x50, 0x00, 0xe7, 0x7d } };
typedef struct vk_interop_surface vk_interop_surface;
struct vk_interop_surface {
	struct {
		HRESULT(STDMETHODCALLTYPE *QueryInterface)(vk_interop_surface *, REFIID, void **);
		ULONG(STDMETHODCALLTYPE *AddRef)(vk_interop_surface *);
		ULONG(STDMETHODCALLTYPE *Release)(vk_interop_surface *);
		HRESULT(STDMETHODCALLTYPE *GetDevice)(vk_interop_surface *, void **);
		HRESULT(STDMETHODCALLTYPE *GetVulkanImageInfo)(vk_interop_surface *, VkImage *, VkImageLayout *,
							       VkImageCreateInfo *);
	} *v;
};

typedef HRESULT(WINAPI *create_device_t)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT,
					 UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT(WINAPI *create_factory_t)(REFIID, void **);
typedef HRESULT(STDMETHODCALLTYPE *create_sc_t)(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **);
typedef HRESULT(STDMETHODCALLTYPE *present_t)(IDXGISwapChain *, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *create_ps_t)(ID3D11Device *, const void *, SIZE_T, ID3D11ClassLinkage *,
						ID3D11PixelShader **);

static struct {
	create_device_t create_device;
	create_factory_t create_factory;
	create_sc_t create_sc;
	present_t present;
	create_ps_t create_ps;
	uint64_t fog_edits, fog_fail, ps_n;
	ID3D11Device *dev;
	ID3D11DeviceContext *ctx;
	int test, filled_dev, filled_sc, iat;
	/* the present pacing */
	LARGE_INTEGER f, last, t_log;
	uint64_t n, n_win, n_test;
	double sum, max;
	unsigned hist[8]; /* < 8, 12, 17, 25, 34, 50, 100 ms, more */
	/* the stand-in's eye textures */
	ID3D11Texture2D *eye[2];
	VkImage eye_img[2];
	int eye_state; /* 0 = not yet, 1 = made, -1 = failed */
} D;

/* the exe's IAT entry of dll!fn (by name), NULL if not imported */
static void **iat_slot(uintptr_t base, const char *dll, const char *fn)
{
	const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
	const IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dd->VirtualAddress)
		return NULL;
	for (const IMAGE_IMPORT_DESCRIPTOR *d = (const void *)(base + dd->VirtualAddress); d->Name; d++) {
		if (_stricmp((const char *)(base + d->Name), dll))
			continue;
		const IMAGE_THUNK_DATA64 *names = (const void *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
		IMAGE_THUNK_DATA64 *iat = (void *)(base + d->FirstThunk);
		for (int i = 0; names[i].u1.AddressOfData; i++) {
			if (IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal))
				continue;
			const IMAGE_IMPORT_BY_NAME *ibn = (const void *)(base + names[i].u1.AddressOfData);
			if (!strcmp((const char *)ibn->Name, fn))
				return (void **)&iat[i].u1.Function;
		}
	}
	return NULL;
}

static int put_ptr(void **at, void *v)
{
	DWORD old, o2;
	if (!VirtualProtect(at, sizeof *at, PAGE_READWRITE, &old))
		return -1;
	*at = v;
	VirtualProtect(at, sizeof *at, old, &o2);
	return 0;
}

static void fill_rows(const char *iface, void **vtbl)
{
	int n, done = 0, tried = 0;
	const struct somvr_win_com *r = somvr_win_com_rows(&n);
	for (int i = 0; i < n; i++)
		if (!strcmp(r[i].iface, iface)) {
			tried++;
			done += somvr_win_com_fill(r[i].name, vtbl) == 0;
		}
	somvr_win_log("COM %s vtable %p: %d of %d rows filled", iface, (void *)vtbl, done, tried);
}

static VkImage vk_image_of(ID3D11Texture2D *t, VkImageLayout *lay, VkImageCreateInfo *ci)
{
	vk_interop_surface *s = NULL;
	VkImage img = VK_NULL_HANDLE;
	if (FAILED(ID3D11Texture2D_QueryInterface(t, &IID_VkInteropSurface, (void **)&s)) || !s)
		return VK_NULL_HANDLE;
	memset(ci, 0, sizeof *ci);
	ci->sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	if (FAILED(s->v->GetVulkanImageInfo(s, &img, lay, ci)))
		img = VK_NULL_HANDLE;
	s->v->Release(s);
	return img;
}

static void xl_note_image(uint64_t img, VkImageLayout lay);

/* winport-stereo: the VkImage behind a D3D11 texture of the mod's own (the eye textures H8 copies into, SOMVR_HUD's
 * HUD texture, the hands' depth) for the layer - somvr.c's tex_vkimage on Windows; 0 = none (not DXVK) */
uint64_t somvr_win_tex_vkimage(const void *tex);
uint64_t somvr_win_tex_vkimage(const void *tex)
{
	VkImageLayout lay = 0;
	VkImageCreateInfo ci;
	uint64_t img = tex ? (uint64_t)vk_image_of((ID3D11Texture2D *)tex, &lay, &ci) : 0;
	if (img) /* its idle layout for the layer's copies (somvr_xrloc v2) */
		xl_note_image(img, lay);
	return img;
}

/* one VkImage of ours and its idle layout into somvr_xrloc (the render thread; a new image only) */
static void xl_note_image(uint64_t img, VkImageLayout lay)
{
	static SRWLOCK mx = SRWLOCK_INIT;
	AcquireSRWLockExclusive(&mx);
	uint32_t n = atomic_load(&g_xrloc_obj.n_img);
	int known = 0;
	for (uint32_t i = 0; i < n; i++)
		if (g_xrloc_obj.img[i] == img)
			known = 1, g_xrloc_obj.img_layout[i] = (uint32_t)lay;
	if (!known && n < SOMVR_XRLOC_NIMG) {
		g_xrloc_obj.img[n] = img, g_xrloc_obj.img_layout[n] = (uint32_t)lay;
		atomic_store_explicit(&g_xrloc_obj.n_img, n + 1, memory_order_release);
		somvr_win_log("INTEROP VkImage %#llx idle layout %d (the layer copies from and back to it)",
			      (unsigned long long)img, lay);
	}
	ReleaseSRWLockExclusive(&mx);
}

static void make_eyes(ID3D11Texture2D *bb)
{
	D3D11_TEXTURE2D_DESC d;
	ID3D11Texture2D_GetDesc(bb, &d);
	atomic_store(&g_sh_obj.eyes.expect_w, d.Width), atomic_store(&g_sh_obj.eyes.expect_h, d.Height);
	d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, d.MiscFlags = 0, d.CPUAccessFlags = 0;
	d.Usage = D3D11_USAGE_DEFAULT, d.MipLevels = 1, d.ArraySize = 1;
	somvr_eyes_info info = { .gen = 1, .w = d.Width, .h = d.Height, .dxgi_format = (uint32_t)d.Format };
	for (int e = 0; e < 2; e++) {
		VkImageLayout lay = 0;
		VkImageCreateInfo ci;
		if (FAILED(ID3D11Device_CreateTexture2D(D.dev, &d, NULL, &D.eye[e])) ||
		    !(D.eye_img[e] = vk_image_of(D.eye[e], &lay, &ci))) {
			somvr_win_log("TEST eye %d: texture or its VkImage failed", e);
			D.eye_state = -1;
			return;
		}
		info.image[e] = (uint64_t)D.eye_img[e];
		xl_note_image(info.image[e], lay);
		somvr_win_log("TEST eye %d: VkImage %#llx %ux%u vkformat %d usage 0x%x layout %d (the layer's log must name it "
			      "an eye image candidate)", e, (unsigned long long)D.eye_img[e], ci.extent.width, ci.extent.height,
			      ci.format, ci.usage, lay);
	}
	somvr_eyes_publish(&g_sh_obj, &info);
	D.eye_state = 1;
}

static void pacing(void)
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	if (D.last.QuadPart) {
		double ms = 1e3 * (double)(t.QuadPart - D.last.QuadPart) / (double)D.f.QuadPart;
		static const double lim[7] = { 8, 12, 17, 25, 34, 50, 100 };
		int b = 0;
		while (b < 7 && ms >= lim[b])
			b++;
		D.hist[b]++, D.sum += ms, D.n_win++;
		if (ms > D.max)
			D.max = ms;
	} else {
		D.t_log = t;
	}
	D.last = t, D.n++;
	if ((double)(t.QuadPart - D.t_log.QuadPart) / (double)D.f.QuadPart >= 10.0 && D.n_win) {
		somvr_win_log("PRESENT n=%llu: %.1f fps, mean %.2f ms, max %.1f ms; <8 %u, <12 %u, <17 %u, <25 %u, <34 %u, <50 %u, "
			      "<100 %u, more %u", (unsigned long long)D.n, 1e3 * (double)D.n_win / D.sum, D.sum / (double)D.n_win,
			      D.max, D.hist[0], D.hist[1], D.hist[2], D.hist[3], D.hist[4], D.hist[5], D.hist[6], D.hist[7]);
		memset(D.hist, 0, sizeof D.hist), D.sum = D.max = 0, D.n_win = 0, D.t_log = t;
	}
}

static HRESULT STDMETHODCALLTYPE my_present(IDXGISwapChain *sc, UINT sync, UINT flags)
{
	if (!D.filled_sc) {
		D.filled_sc = 1;
		fill_rows("IDXGISwapChain", *(void ***)sc);
		ID3D11Texture2D *bb = NULL;
		if (SUCCEEDED(IDXGISwapChain_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&bb)) && bb) {
			fill_rows("ID3D11Texture2D", *(void ***)bb);
			VkImageLayout lay = 0;
			VkImageCreateInfo ci;
			VkImage img = vk_image_of(bb, &lay, &ci);
			somvr_win_log("INTEROP the back buffer: VkImage %#llx %ux%u vkformat %d layout %d (%s)",
				      (unsigned long long)img, ci.extent.width, ci.extent.height, ci.format, lay,
				      img ? "IDXGIVkInteropSurface::GetVulkanImageInfo" : "no DXVK interop: not DXVK?");
			somvr_core_win_com();
			somvr_win_late_h9();
			ID3D11Texture2D_Release(bb);
		}
	}
	/* the stand-in's eye textures once the layer's session runs (as H8 makes them: the layer's image hook then
	 * knows somvr_shared and remembers images of the announced size) */
	if (D.test && D.dev && !D.eye_state &&
	    atomic_load_explicit(&g_sh_obj.xr.state, memory_order_acquire) == SOMVR_XRS_RUNNING) {
		ID3D11Texture2D *bb = NULL;
		if (SUCCEEDED(IDXGISwapChain_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&bb)) && bb) {
			make_eyes(bb);
			ID3D11Texture2D_Release(bb);
		}
	}
	if (D.test && D.eye_state == 1 && D.ctx) {
		ID3D11Texture2D *bb = NULL;
		if (SUCCEEDED(IDXGISwapChain_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&bb)) && bb) {
			ID3D11DeviceContext_CopyResource(D.ctx, (ID3D11Resource *)D.eye[0], (ID3D11Resource *)bb);
			ID3D11DeviceContext_CopyResource(D.ctx, (ID3D11Resource *)D.eye[1], (ID3D11Resource *)bb);
			ID3D11Texture2D_Release(bb);
			atomic_fetch_add(&g_sh_obj.eyes.copies, 2);
		}
	}
	if (flags & DXGI_PRESENT_TEST)
		D.n_test++;
	else
		pacing();
	return D.present(sc, sync, flags);
}

/* SOMVR_XR_FOG=radial (docs/re-winport.md 7.5): every pixel shader's DXBC through somvr_fogdxbc; an edited blob is
 * created in its place (the fog overlay's two shaders), every other blob untouched */
static HRESULT STDMETHODCALLTYPE my_create_ps(ID3D11Device *dev, const void *bc, SIZE_T n, ID3D11ClassLinkage *cl,
					      ID3D11PixelShader **out)
{
	D.ps_n++;
	size_t nn = 0;
	const char *why = NULL;
	unsigned char *ed = bc && n > 32 ? somvr_fogdxbc(bc, n, &nn, &why) : NULL;
	if (!ed) {
		int fog = 0; /* the planet's radius in it: a fog shader the edit did not take */
		for (SIZE_T i = 0; bc && i + 4 <= n && !fog; i += 4)
			fog = *(const uint32_t *)((const char *)bc + i) == 0x4e17e580u;
		if (fog) {
			D.fog_fail++;
			somvr_win_log("FOG shader %u bytes holds l(6.371e8) but stays the game's: %s", (unsigned)n, why ? why : "?");
		}
		return D.create_ps(dev, bc, n, cl, out);
	}
	HRESULT hr = D.create_ps(dev, ed, nn, cl, out);
	const unsigned char *c = ed + 4;
	somvr_win_log("FOG shader %02x%02x%02x%02x%02x%02x%02x%02x... created with the fog by the distance (%u -> %u bytes) "
		      "-> %#lx", c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], (unsigned)n, (unsigned)nn, (unsigned long)hr);
	free(ed);
	if (SUCCEEDED(hr))
		D.fog_edits++;
	else /* the edited blob refused: the game's own */
		hr = D.create_ps(dev, bc, n, cl, out);
	return hr;
}

static HRESULT STDMETHODCALLTYPE my_create_sc(IDXGIFactory *f, IUnknown *dev, DXGI_SWAP_CHAIN_DESC *d, IDXGISwapChain **out)
{
	HRESULT hr = D.create_sc(f, dev, d, out);
	somvr_win_log("D3D CreateSwapChain %ux%u format %d buffers %u windowed %d -> %#lx", d ? d->BufferDesc.Width : 0,
		      d ? d->BufferDesc.Height : 0, d ? (int)d->BufferDesc.Format : -1, d ? d->BufferCount : 0,
		      d ? d->Windowed : -1, (unsigned long)hr);
	if (SUCCEEDED(hr) && out && *out) {
		void **vt = *(void ***)*out;
		if (vt[8] != (void *)my_present) {
			if (!D.present)
				D.present = (present_t)vt[8];
			if ((present_t)vt[8] == D.present && put_ptr(&vt[8], (void *)my_present) == 0)
				somvr_win_log("D3D Present hooked (vtable %p slot 8, was %p)", (void *)vt, (void *)D.present);
			else
				somvr_win_log("D3D Present NOT hooked: another Present %p in vtable %p", vt[8], (void *)vt);
		}
	}
	return hr;
}

static HRESULT WINAPI my_create_factory(REFIID riid, void **pp)
{
	HRESULT hr = D.create_factory(riid, pp);
	if (SUCCEEDED(hr) && pp && *pp) {
		void **vt = *(void ***)*pp;
		if (vt[10] != (void *)my_create_sc) {
			if (!D.create_sc)
				D.create_sc = (create_sc_t)vt[10];
			if ((create_sc_t)vt[10] == D.create_sc)
				put_ptr(&vt[10], (void *)my_create_sc);
		}
		somvr_win_log("D3D CreateDXGIFactory -> %#lx, CreateSwapChain %s", (unsigned long)hr,
			      vt[10] == (void *)my_create_sc ? "hooked" : "NOT hooked (another factory class)");
	}
	return hr;
}

static HRESULT WINAPI my_create_device(IDXGIAdapter *a, D3D_DRIVER_TYPE t, HMODULE sw, UINT fl, const D3D_FEATURE_LEVEL *lv,
				       UINT nlv, UINT sdk, ID3D11Device **dev, D3D_FEATURE_LEVEL *got, ID3D11DeviceContext **ctx)
{
	HRESULT hr = D.create_device(a, t, sw, fl, lv, nlv, sdk, dev, got, ctx);
	somvr_win_log("D3D D3D11CreateDevice flags %#x -> %#lx (level %#x)", fl, (unsigned long)hr, got ? (unsigned)*got : 0);
	if (SUCCEEDED(hr) && dev && *dev && !D.filled_dev) {
		D.filled_dev = 1;
		D.dev = *dev;
		fill_rows("ID3D11Device", *(void ***)*dev);
		const char *fog = getenv("SOMVR_XR_FOG");
		void **vt = *(void ***)*dev;
		if (fog && !strcmp(fog, "radial") && vt[15] != (void *)my_create_ps) {
			D.create_ps = (create_ps_t)vt[15];
			somvr_win_log("FOG SOMVR_XR_FOG=radial: ID3D11Device::CreatePixelShader (slot 15) %s",
				      put_ptr(&vt[15], (void *)my_create_ps) == 0 ? "hooked" : "NOT hooked");
		}
		ID3D11DeviceContext *c = ctx && *ctx ? *ctx : NULL;
		if (!c)
			ID3D11Device_GetImmediateContext(*dev, &c); /* (a reference we keep) */
		D.ctx = c;
		if (c)
			fill_rows("ID3D11DeviceContext", *(void ***)c);
	}
	return hr;
}

/* DllMain: the two IAT entries (the exe's static imports; d3d11/dxgi are loaded by then) */
void somvr_win_d3d_attach(uintptr_t exe, int test)
{
	QueryPerformanceFrequency(&D.f);
	D.test = test;
	void **a = iat_slot(exe, "d3d11.dll", "D3D11CreateDevice"), **b = iat_slot(exe, "dxgi.dll", "CreateDXGIFactory");
	if (a && *a) {
		D.create_device = (create_device_t)*a;
		D.iat += put_ptr(a, (void *)my_create_device) == 0;
	}
	if (b && *b) {
		D.create_factory = (create_factory_t)*b;
		D.iat += put_ptr(b, (void *)my_create_factory) == 0;
	}
	somvr_win_log("D3D IAT: D3D11CreateDevice %s, CreateDXGIFactory %s", a ? "hooked" : "not imported",
		      b ? "hooked" : "not imported");
}

/* ---- the stand-in's locate client: what the game thread does, through the PE stand-ins ------------------------- */
static DWORD WINAPI xl_client(void *p)
{
	(void)p;
	somvr_views_state st;
	memset(&st, 0, sizeof st);
	uint64_t ok = 0, fresh = 0, fail = 0, grips = 0, gfail = 0;
	LARGE_INTEGER f, t0, t;
	QueryPerformanceFrequency(&f), QueryPerformanceCounter(&t0);
	for (;;) {
		somvr_views v;
		somvr_views_locate(&g_sh_obj, &st, &v);
		ok += v.ok, fresh += v.fresh, fail += !v.ok;
		PFN_xrLocateSpace ls = (PFN_xrLocateSpace)g_in_obj.xr.locate_space;
		if (v.fresh && ls && g_xrloc_obj.space[0]) {
			XrSpaceLocation l = { .type = XR_TYPE_SPACE_LOCATION };
			XrResult r = ls((XrSpace)(uintptr_t)g_xrloc_obj.space[0], (XrSpace)(uintptr_t)g_sh_obj.xr.space, v.time, &l);
			grips += XR_SUCCEEDED(r), gfail += XR_FAILED(r);
		}
		QueryPerformanceCounter(&t);
		if ((double)(t.QuadPart - t0.QuadPart) / (double)f.QuadPart >= 5.0) {
			somvr_win_log("XRLOC views ok %llu fresh %llu fail %llu (reused %llu); head L %.3f %.3f %.3f at t=%lld; grip L "
				      "located %llu failed %llu; layer ticks %llu, stale/none %llu",
				      (unsigned long long)ok, (unsigned long long)fresh, (unsigned long long)fail,
				      (unsigned long long)st.reused, v.pose[0].p[0], v.pose[0].p[1], v.pose[0].p[2],
				      (long long)v.time, (unsigned long long)grips, (unsigned long long)gfail,
				      (unsigned long long)atomic_load(&g_xrloc_obj.ticks),
				      (unsigned long long)atomic_load(&g_xrloc_obj.n_stale));
			t0 = t;
		}
		Sleep(11);
	}
	return 0;
}

void somvr_win_d3d_test_start(void)
{
	HANDLE h = CreateThread(NULL, 0, xl_client, NULL, 0, NULL);
	if (h)
		CloseHandle(h);
}
