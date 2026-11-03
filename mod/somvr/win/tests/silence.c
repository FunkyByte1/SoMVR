/* silence.c - the Windows launcher's audio probe (scripts/som-win-audio-probe.sh): plays SECONDS of digital silence
 * through the default render endpoint the way Wwise opens it (WASAPI: IMMDeviceEnumerator::GetDefaultAudioEndpoint
 * (eRender, eConsole), a shared-mode IAudioClient, every buffer flagged AUDCLNT_BUFFERFLAGS_SILENT and zero-filled), so
 * the PulseAudio stream Wine makes for it can be looked at (which sink, which properties) with no sound at all.
 *   silence.exe [SECONDS]   (default 4) -> stdout: the endpoint's id and name, the mix format, the frames written */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DEFINE_GUID(SV_CLSID_MMDeviceEnumerator, 0xbcde0395, 0xe52f, 0x467c, 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e);
DEFINE_GUID(SV_IID_IMMDeviceEnumerator, 0xa95664d2, 0x9614, 0x4f35, 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6);
DEFINE_GUID(SV_IID_IAudioClient, 0x1cb9ad4c, 0xdbfa, 0x4c32, 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2);
DEFINE_GUID(SV_IID_IAudioRenderClient, 0xf294acfc, 0x3146, 0x4483, 0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2);

#define CHK(what, hr) do { if (FAILED(hr)) { printf("silence: %s failed 0x%08lx\n", what, (unsigned long)(hr)); return 1; } } while (0)

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 4;
	if (secs < 1 || secs > 60)
		secs = 4;
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	CHK("CoInitializeEx", hr);
	IMMDeviceEnumerator *en = NULL;
	hr = CoCreateInstance(&SV_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &SV_IID_IMMDeviceEnumerator, (void **)&en);
	CHK("CoCreateInstance(MMDeviceEnumerator)", hr);
	IMMDevice *dev = NULL;
	hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev);
	CHK("GetDefaultAudioEndpoint", hr);
	LPWSTR id = NULL;
	if (SUCCEEDED(IMMDevice_GetId(dev, &id)) && id)
		printf("silence: default endpoint id %ls\n", id);
	IPropertyStore *ps = NULL;
	if (SUCCEEDED(IMMDevice_OpenPropertyStore(dev, STGM_READ, &ps)) && ps) {
		PROPVARIANT v;
		PropVariantInit(&v);
		if (SUCCEEDED(IPropertyStore_GetValue(ps, &PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
			printf("silence: default endpoint name %ls\n", v.pwszVal);
		PropVariantClear(&v);
	}
	IAudioClient *ac = NULL;
	hr = IMMDevice_Activate(dev, &SV_IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&ac);
	CHK("Activate(IAudioClient)", hr);
	WAVEFORMATEX *wf = NULL;
	hr = IAudioClient_GetMixFormat(ac, &wf);
	CHK("GetMixFormat", hr);
	printf("silence: mix format %u ch, %lu Hz, %u bits\n", wf->nChannels, (unsigned long)wf->nSamplesPerSec, wf->wBitsPerSample);
	hr = IAudioClient_Initialize(ac, AUDCLNT_SHAREMODE_SHARED, 0, 2000000 /* 200 ms */, 0, wf, NULL);
	CHK("Initialize", hr);
	UINT32 nbuf = 0;
	IAudioClient_GetBufferSize(ac, &nbuf);
	IAudioRenderClient *rc = NULL;
	hr = IAudioClient_GetService(ac, &SV_IID_IAudioRenderClient, (void **)&rc);
	CHK("GetService(IAudioRenderClient)", hr);
	hr = IAudioClient_Start(ac);
	CHK("Start", hr);
	unsigned long long written = 0;
	DWORD t0 = GetTickCount();
	while (GetTickCount() - t0 < (DWORD)secs * 1000) {
		UINT32 pad = 0;
		IAudioClient_GetCurrentPadding(ac, &pad);
		UINT32 n = nbuf - pad;
		if (n) {
			BYTE *p = NULL;
			if (SUCCEEDED(IAudioRenderClient_GetBuffer(rc, n, &p)) && p) {
				memset(p, 0, (size_t)n * wf->nBlockAlign); /* zeros, and flagged silent */
				IAudioRenderClient_ReleaseBuffer(rc, n, AUDCLNT_BUFFERFLAGS_SILENT);
				written += n;
			}
		}
		Sleep(20);
	}
	IAudioClient_Stop(ac);
	printf("silence: %llu frames of silence written in %d s (buffer %u frames)\n", written, secs, nbuf);
	return 0;
}
