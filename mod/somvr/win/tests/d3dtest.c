/* d3dtest.c - the harness's stand-in for the game under Proton (scripts/headless_win.sh --test): it imports
 * XINPUT9_1_0.dll, d3d11!D3D11CreateDevice and dxgi!CreateDXGIFactory exactly as ShadowOfMordor.exe does, so the DLL
 * loads, gates (SOMVR_WIN_TEST=1), hooks and bridges the same way.
 *   d3dtest.exe xinput             XInputGetState(0) once, then exit (the DLL's load, gate, entry and bridge)
 *   d3dtest.exe d3d SECONDS [W H]  a window, CreateDXGIFactory -> D3D11CreateDevice -> CreateSwapChain, a colour
 *                                  cycle presented at the display's rate for SECONDS, then exit
 *   d3dtest.exe vrinit             Proton's steam.exe's VR setup for wineopenxr (vrclient's vrclient_init_registry
 *                                  does this at a Steam launch; the harness runs wine directly): wineopenxr.dll's
 *                                  wineopenxr_init_registry() (the host runtime's Vulkan extensions into
 *                                  HKCU\Software\Wine\VR), then its "state" = 1 (ready), which wineopenxr's
 *                                  xrCreateInstance waits for. Only for the W1 test's own prefix (headless_win.sh)
 * Exit code 0 = every call succeeded. Writes nothing but its stdout. */
#define COBJMACROS
#define INITGUID
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xinput.h>

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_CLOSE)
		return PostQuitMessage(0), 0;
	return DefWindowProcW(h, m, w, l);
}

static int run_d3d(double seconds, int w, int h)
{
	WNDCLASSW wc = { .lpfnWndProc = wndproc, .hInstance = GetModuleHandleW(NULL), .lpszClassName = L"somvr_d3dtest" };
	RegisterClassW(&wc);
	RECT r = { 0, 0, w, h };
	AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
	HWND wnd = CreateWindowW(L"somvr_d3dtest", L"somvr d3dtest", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0,
				 r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
	if (!wnd)
		return printf("d3dtest: CreateWindow failed %lu\n", GetLastError()), 2;
	IDXGIFactory *fac = NULL;
	HRESULT hr = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&fac);
	if (FAILED(hr))
		return printf("d3dtest: CreateDXGIFactory %#lx\n", (unsigned long)hr), 3;
	ID3D11Device *dev = NULL;
	ID3D11DeviceContext *ctx = NULL;
	D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, got;
	hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &fl, 1, D3D11_SDK_VERSION, &dev, &got, &ctx);
	if (FAILED(hr))
		return printf("d3dtest: D3D11CreateDevice %#lx\n", (unsigned long)hr), 4;
	DXGI_SWAP_CHAIN_DESC sd = { .BufferDesc = { .Width = (UINT)w, .Height = (UINT)h,
						     .Format = DXGI_FORMAT_R8G8B8A8_UNORM },
				    .SampleDesc = { 1, 0 }, .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
				    .BufferCount = 2, .OutputWindow = wnd, .Windowed = TRUE,
				    .SwapEffect = DXGI_SWAP_EFFECT_DISCARD };
	IDXGISwapChain *sc = NULL;
	hr = IDXGIFactory_CreateSwapChain(fac, (IUnknown *)dev, &sd, &sc);
	if (FAILED(hr))
		return printf("d3dtest: CreateSwapChain %#lx\n", (unsigned long)hr), 5;
	ID3D11Texture2D *bb = NULL;
	ID3D11RenderTargetView *rtv = NULL;
	IDXGISwapChain_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&bb);
	ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)bb, NULL, &rtv);
	LARGE_INTEGER f, t0, t;
	QueryPerformanceFrequency(&f), QueryPerformanceCounter(&t0);
	unsigned frames = 0;
	for (;;) {
		MSG m;
		while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
			if (m.message == WM_QUIT)
				goto done;
			TranslateMessage(&m), DispatchMessageW(&m);
		}
		QueryPerformanceCounter(&t);
		double s = (double)(t.QuadPart - t0.QuadPart) / (double)f.QuadPart;
		if (s >= seconds)
			break;
		float c[4] = { 0.5f + 0.5f * (float)sin(s * 2.0), 0.3f, 0.5f + 0.5f * (float)cos(s * 1.3), 1.0f };
		ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
		ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, c);
		hr = IDXGISwapChain_Present(sc, 1, 0);
		if (FAILED(hr))
			return printf("d3dtest: Present %#lx after %u frames\n", (unsigned long)hr, frames), 6;
		frames++;
	}
done:
	QueryPerformanceCounter(&t);
	printf("d3dtest: %u frames in %.1f s (%.1f fps)\n", frames, (double)(t.QuadPart - t0.QuadPart) / (double)f.QuadPart,
	       frames / ((double)(t.QuadPart - t0.QuadPart) / (double)f.QuadPart));
	ID3D11RenderTargetView_Release(rtv), ID3D11Texture2D_Release(bb), IDXGISwapChain_Release(sc);
	ID3D11DeviceContext_Release(ctx), ID3D11Device_Release(dev), IDXGIFactory_Release(fac);
	return 0;
}

/* the DLL's hook test (SOMVR_WIN_TEST): it hooks d3dtest_f1's entry (+1000 on the result) and puts a mid-function
 * detour on d3dtest_f2 (its first argument +5) before main runs */
__declspec(dllexport) __declspec(noinline) int d3dtest_f1(int a, int b) { return a * 10 + b; }
__declspec(dllexport) __declspec(noinline) int d3dtest_f2(int a, int b) { return a * 10 + b; }
static int (*volatile f1p)(int, int) = d3dtest_f1, (*volatile f2p)(int, int) = d3dtest_f2;

static int run_vrinit(void)
{
	HMODULE m = LoadLibraryA("wineopenxr.dll");
	FARPROC f = m ? GetProcAddress(m, "wineopenxr_init_registry") : NULL;
	if (!f) {
		printf("d3dtest: vrinit: no wineopenxr.dll / wineopenxr_init_registry (%lu)\n", GetLastError());
		return 1;
	}
	/* the key first (wineopenxr_init_registry opens it, as steam.exe's VR setup made it), state 0 = not ready */
	HKEY k;
	DWORD zero = 0, one = 1;
	LONG c = RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\VR", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
	if (c == ERROR_SUCCESS)
		RegSetValueExA(k, "state", 0, REG_DWORD, (const BYTE *)&zero, sizeof zero);
	BOOL r = ((BOOL(WINAPI *)(void))f)();
	LONG v = c == ERROR_SUCCESS ? RegSetValueExA(k, "state", 0, REG_DWORD, (const BYTE *)&one, sizeof one) : c;
	char ie[1024] = "";
	DWORD n = sizeof ie;
	if (c == ERROR_SUCCESS) {
		RegQueryValueExA(k, "openxr_vulkan_instance_extensions", NULL, NULL, (BYTE *)ie, &n);
		RegCloseKey(k);
	}
	printf("d3dtest: vrinit: wineopenxr_init_registry = %d, state=1 %s; openxr_vulkan_instance_extensions '%s'\n",
	       (int)r, v == ERROR_SUCCESS ? "set" : "NOT set", ie);
	return r && v == ERROR_SUCCESS && ie[0] ? 0 : 1; /* (a BOOL: TRUE = the entries written) */
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "vrinit"))
		return run_vrinit();
	XINPUT_STATE st;
	DWORD xr = XInputGetState(0, &st);
	printf("d3dtest: XInputGetState(0) = %lu\n", xr);
	printf("d3dtest: hooks f1(1,2) = %d (hooked 1012, plain 12), f2(3,4) = %d (hooked 84, plain 34)\n", f1p(1, 2), f2p(3, 4));
	if (argc >= 3 && !strcmp(argv[1], "d3d"))
		return run_d3d(atof(argv[2]), argc >= 5 ? atoi(argv[3]) : 1280, argc >= 5 ? atoi(argv[4]) : 720);
	return xr == ERROR_SUCCESS || xr == ERROR_DEVICE_NOT_CONNECTED ? 0 : 1;
}
