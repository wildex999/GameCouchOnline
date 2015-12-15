#include "stdafx.h"

#include "D3D9Hooks.h"
#include "common.h"
#include "D3D9MethodOffsets.h"

bool localSetupDone = false;
IDirect3DDevice9* currentDevice;

int frameCount = 0;
int dropRate = 0;
int dropCounter = 0;
LARGE_INTEGER s_StartingTime, s_EndingTime, s_ElapsedMicroseconds;
LARGE_INTEGER s_Frequency;

//Hooks
FunctionHook hookEndScene;
FunctionHook hookPresent;
FunctionHook hookPresentEx;
FunctionHook hookSwapPresent;
FunctionHook hookReset;
FunctionHook hookResetEx;

typedef HRESULT(STDMETHODCALLTYPE *D3D9EndSceneFunc)(IDirect3DDevice9 *device);
typedef HRESULT(STDMETHODCALLTYPE *D3D9PresentFunc)(IDirect3DDevice9 *device, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion);
typedef HRESULT(STDMETHODCALLTYPE *D3D9PresentExFunc)(IDirect3DDevice9Ex *device, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags);
typedef HRESULT(STDMETHODCALLTYPE *D3D9SwapPresentFunc)(IDirect3DSwapChain9* swap, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags);
typedef HRESULT(STDMETHODCALLTYPE *D3D9ResetFunc)(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *params);
typedef HRESULT(STDMETHODCALLTYPE *D3D9ResetExFunc)(IDirect3DDevice9Ex *device, D3DPRESENT_PARAMETERS *params, D3DDISPLAYMODEEX *pFullsceenDisplayMode);

HRESULT STDMETHODCALLTYPE Hooked_D3D9EndScene(IDirect3DDevice9 *device) {
	if (!localSetupDone)
	{
		currentDevice = device;
		localSetup(device);
		localSetupDone = true;

		QueryPerformanceFrequency(&s_Frequency);
		QueryPerformanceCounter(&s_StartingTime);
	}

	if (captureTask != NULL)
	{
		bool reInit = false;
		//New device check
		if (currentDevice != device)
		{
			logOutput << "New device!" << std::endl;
			currentDevice = device;
			reInit = true;
		}
		
		//New render target check
		IDirect3DSurface9* surf;
		if (!FAILED(device->GetRenderTarget(0, &surf)))
		{
			if (surf != captureTask->getRenderSurface())
				reInit = true;
			surf->Release();
		}

		//Resize request check
		if(captureTask->lock(true))
		{
			if (captureTask->targetWidth != captureTask->newWidth || captureTask->targetHeight != captureTask->newHeight)
				reInit = true;
			captureTask->unlock();
		}

		if (reInit)
			captureTask->reInitialize(device);
	}

	return ((D3D9EndSceneFunc)hookEndScene.getOriginalFunction())(device);
}

HRESULT STDMETHODCALLTYPE Hooked_D3D9Present(IDirect3DDevice9 *device, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion) {
	HRESULT result = ((D3D9PresentFunc)hookPresent.getOriginalFunction())(device, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
	doCapture(device);
	return result;
}

HRESULT STDMETHODCALLTYPE Hooked_D3D9PresentEx(IDirect3DDevice9Ex *device, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags) {
	LogCurrentTime(); logOutput << "PresentEx" << std::endl;
	return ((D3D9PresentExFunc)hookPresentEx.getOriginalFunction())(device, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
}

HRESULT STDMETHODCALLTYPE Hooked_D3D9SwapPresent(IDirect3DSwapChain9 *swap, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags) {
	HRESULT result = ((D3D9SwapPresentFunc)hookSwapPresent.getOriginalFunction())(swap, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
	doCapture(currentDevice);
	return result;
}

HRESULT STDMETHODCALLTYPE Hooked_D3D9Reset(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *params) {
	LogCurrentTime(); logOutput << "Reset device" << std::endl; logOutput.flush();

	if (captureTask)
		captureTask->deInitialize();

	HRESULT result = ((D3D9ResetFunc)hookReset.getOriginalFunction())(device, params);
	localSetupDone = false;

	return result;
}

HRESULT STDMETHODCALLTYPE Hooked_D3D9ResetEx(IDirect3DDevice9Ex *device, D3DPRESENT_PARAMETERS *params, D3DDISPLAYMODEEX *pFullsceenDisplayMode)
{
	LogCurrentTime(); logOutput << "ResetEx Device" << std::endl; logOutput.flush();
	HRESULT result = ((D3D9ResetExFunc)hookResetEx.getOriginalFunction())(device, params, pFullsceenDisplayMode);
	return result;
}

bool localSetup(IDirect3DDevice9 *device) {
	//Hook the swap chain
	IDirect3DSwapChain9 *swapChain = NULL;
	if (FAILED(device->GetSwapChain(0, &swapChain)))
	{
		LogCurrentTime(); logOutput << "Failed to get swap chain" << std::endl;
		return false;
	}

	unsigned long* vtable = *(unsigned long**)swapChain;

	if (!hookSwapPresent.hook((FARPROC)*(vtable + D3D9SwapOffset_Present), (FARPROC)Hooked_D3D9SwapPresent))
	{
		LogCurrentTime();logOutput << "Failed to hook Swapchain Present" << std::endl;
		swapChain->Release();
		return false;
	}

	swapChain->Release();
	return true;
}

bool doCapture(IDirect3DDevice9 *device) {
	LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
	LARGE_INTEGER Frequency;

	/*QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&StartingTime);*/

	//Framerate count
	QueryPerformanceCounter(&s_EndingTime);
	s_ElapsedMicroseconds.QuadPart = s_EndingTime.QuadPart - s_StartingTime.QuadPart;
	s_ElapsedMicroseconds.QuadPart *= 1000000;
	s_ElapsedMicroseconds.QuadPart /= s_Frequency.QuadPart;
	long timeMs = s_ElapsedMicroseconds.QuadPart / 1000;
	if (timeMs >= 1000)
	{
		QueryPerformanceCounter(&s_StartingTime);
		LogCurrentTime(); logOutput << "FPS: " << frameCount << ", DropRate: " << dropRate << std::endl;

		if (frameCount < 60) //TODO: Allow for setting target FPS
		{
			dropRate++;
		}
		else
			dropRate--;

		if (dropRate > 2)
			dropRate = 2;
		else if (dropRate < -2)
			dropRate = -2;

		frameCount = 0;
	}
	frameCount++;

	dropCounter++;
	if (dropCounter < dropRate)
		return true;

	dropCounter = 0;

	if (captureTask == NULL)
	{
		captureTask = new D3D9CaptureTask(device);
		if (!captureTask->isInitialized())
		{
			logOutput << "Failed to initialize CaptureTask" << std::endl;
			delete captureTask;
			return false;
		}
	}

	if (captureTask->lock(true))
	{
		if (!captureTask->captureToSystemMemory())
			logOutput << "Failed to captureToSystemMemory" << std::endl;


		captureTask->unlock();
		SetEvent(eventGotFrame);
	}
	else
	{
		/*LogCurrentTime();
		logOutput << "Drop frame" << std::endl;*/
	}

	/*QueryPerformanceCounter(&EndingTime);
	ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;
	ElapsedMicroseconds.QuadPart *= 1000000;
	ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;
	logOutput << "Time used CaptureToSystem: " << ElapsedMicroseconds.QuadPart << std::endl;*/

	return true;
}