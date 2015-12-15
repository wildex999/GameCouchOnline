// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include <Shlobj.h>
#include <d3d9.h>
#include <stdint.h> //Required by x264.h

#include "D3D9MethodOffsets.h"
#include "D3D9Hooks.h"
#include "common.h"
#include "../GCO_Main/FrameData.h"

extern "C" {
#include <x264.h>
#include <libswscale\swscale.h>
}

using namespace std;

HMODULE dllModule;
HMODULE d3d9Module;
HANDLE captureThreadHandle;
HANDLE eventGotFrame;
HANDLE sharedMemoryFrameData = NULL;
FrameData* viewFrameData = NULL;
HWND dummyWindow;
bool isHooked;

x264_param_t param;
x264_t* encoder = NULL;
x264_picture_t pic_in, pic_out;
struct SwsContext* convertCtx = NULL;
x264_nal_t* nals;
int nalCount;

bool initializeEvents();
void closeEvents();
void cleanHooks();
bool initializeSharedMemory();
void closeSharedMemory();

bool initializeEncoder(int width, int height);
bool encodeFrame(char* data, int pitch, int width, int height);
void freeEncoder();

D3D9CaptureTask* captureTask;
short captureTaskVersion = -1;

typedef IDirect3D9* (WINAPI*D3D9CREATEPROC)(UINT);
bool hookD3D9() {
#define LOGANDFAIL(strstream, out) { logOutput << strstream << endl; goto out; }
	LogCurrentTime();
	logOutput << "Try to hook D3D9" << endl;

	//First we get a handle to the d3d9 dll
	WCHAR d3d9DllPath[MAX_PATH];
	SHGetFolderPath(NULL, CSIDL_SYSTEM, NULL, SHGFP_TYPE_CURRENT, d3d9DllPath);
	wcscat_s(d3d9DllPath, MAX_PATH, TEXT("\\d3d9.dll"));

	d3d9Module = GetModuleHandle(d3d9DllPath);
	if (d3d9Module == NULL)
		LOGANDFAIL("Failed to get d3d9Module with path: " << d3d9DllPath << ", error: " << GetLastError(), out_error);

	D3D9CREATEPROC d3d9Create = (D3D9CREATEPROC)GetProcAddress(d3d9Module, "Direct3DCreate9");
	if (d3d9Create == NULL)
		LOGANDFAIL("Failed to get Direct3DCreate9 from module at path: " << d3d9DllPath << ", error: " << GetLastError(), out_freeModule);

	IDirect3D9 *d3d9 = (*d3d9Create)(D3D_SDK_VERSION);
	if (d3d9 == NULL)
		LOGANDFAIL("Failed to create an IDirect3D9 object for SDK version: " << D3D_SDK_VERSION << ", using module at path: " << d3d9DllPath, out_freeModule);

	D3DPRESENT_PARAMETERS param;
	ZeroMemory(&param, sizeof(param));
	param.Windowed = 1;
	param.SwapEffect = D3DSWAPEFFECT_FLIP;
	param.BackBufferFormat = D3DFMT_UNKNOWN;
	param.BackBufferCount = 1;
	param.hDeviceWindow = dummyWindow;
	param.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

	IDirect3DDevice9* device;
	HRESULT result;
	//We try to create a device that will work in almost all cases without disturbing the existing device
	result = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, dummyWindow, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES, &param, &device);
	if (result != D3D_OK)
		LOGANDFAIL("Failed to create D3D9 Device, using SDK version: " << D3D_SDK_VERSION << ", return value: " << result, out_releaseD3D9);
	//Hook functions
	unsigned long* vtable = *(unsigned long**)device;

	if (!hookEndScene.hook((FARPROC)*(vtable + D3D9Offset_EndScene), (FARPROC)Hooked_D3D9EndScene))
		LOGANDFAIL("Failed to hook EndScene, using SDK version: " << D3D_SDK_VERSION, out_unHook);
	if (!hookPresent.hook((FARPROC)*(vtable + D3D9Offset_Present), (FARPROC)Hooked_D3D9Present))
		LOGANDFAIL("Failed to hook Present, using SDK version: " << D3D_SDK_VERSION, out_unHook);
	if (!hookPresentEx.hook((FARPROC)*(vtable + D3D9Offset_PresentEx), (FARPROC)Hooked_D3D9PresentEx))
		LOGANDFAIL("Failed to hook PresentEx, using SDK version: " << D3D_SDK_VERSION, out_unHook);
	if (!hookReset.hook((FARPROC)*(vtable + D3D9Offset_Reset), (FARPROC)Hooked_D3D9Reset))
		LOGANDFAIL("Failed to hook Reset, using SDK version: " << D3D_SDK_VERSION, out_unHook);
	if (!hookResetEx.hook((FARPROC)*(vtable + D3D9Offset_ResetEx), (FARPROC)Hooked_D3D9ResetEx))
		LOGANDFAIL("Failed to hook ResetEx, using SDK version: " << D3D_SDK_VERSION, out_unHook);


	//Cleanup
	device->Release();
	d3d9->Release();

	LogCurrentTime();
	logOutput << "Hooked D3D9!" << endl;
	isHooked = true;
	return true;

	//Error cleanup(Unwind)
out_unHook:
	cleanHooks();
out_releaseDevice:
	device->Release();
out_releaseD3D9:
	d3d9->Release();
out_freeModule:
	CloseHandle(d3d9Module);
out_error:
	return false;
}

DWORD WINAPI DummyWindowThread(LPVOID unused)
{
	WNDCLASS winClass;
	ZeroMemory(&winClass, sizeof(winClass));
	winClass.style = CS_OWNDC;
	winClass.hInstance = dllModule;
	winClass.lpfnWndProc = (WNDPROC)DefWindowProc;
	winClass.lpszClassName = classNameDummyWindow;

	if (RegisterClass(&winClass)) {
		dummyWindow = CreateWindowEx(0,
			classNameDummyWindow, classNameDummyWindow,
			WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
			0, 0, 1, 1,
			NULL, NULL, dllModule, NULL);

		if (!dummyWindow) {
			logOutput << "Failed to create dummy window, error: " << GetLastError() << endl;
			return 0;
		}
	}
	else {
		logOutput << "Failed to register dummyWindow class, error: " << GetLastError() << endl;
		return 0;
	}
}

DWORD WINAPI CaptureThread(HANDLE mainDllThread)
{
#define LOGANDFAIL(strstream, out) { logOutput << strstream << endl; goto out; }
	logOutput << "Starting Capture thread, waiting for main thread to finish" << endl;
	//Allow the main DLL thread to finish before we move on
	if (mainDllThread)
	{
		WaitForSingleObject(mainDllThread, 500);
		CloseHandle(mainDllThread);
	}
	LogCurrentTime();
	logOutput << "Main thread finished, moving on with Capture thread" << endl;

	isHooked = false;
	captureTask = NULL;

	if (!initializeEvents())
	{
		LogCurrentTime();
		LOGANDFAIL("Failed to initialize events", exitAndDetach);
	}
	if (!initializeSharedMemory())
	{
		LogCurrentTime();
		LOGANDFAIL("Failed to initialize shared memory", freeEventHandles);
	}

	//We need a window when creating the D3D9 device, so we create a dummy window
	HANDLE hWindowThread = CreateThread(NULL, 0, DummyWindowThread, NULL, 0, NULL);
	if (!hWindowThread) {
		LOGANDFAIL("Failed to create DummyWindow thread, error: " << GetLastError(), freeEventHandles);
	}
	CloseHandle(hWindowThread);

	while (true) {
		DWORD endStatus = WaitForSingleObject(eventEndCapture, 0);
		if (endStatus == WAIT_OBJECT_0)
		{
			logOutput << "Got call to exit" << std::endl;
			break;
		}
		else if (endStatus != WAIT_TIMEOUT)
			LOGANDFAIL("Got error while watching EndCapture event: " << endStatus, freeEventHandles);

		if (!isHooked) {
			//Try to hook into D3D9 device
			hookD3D9();
		}
		else {
			//Wait for Frame data
			DWORD result = WaitForSingleObject(eventGotFrame, 10);
			if (result == WAIT_OBJECT_0)
			{

				captureTask->lock(false);

				LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
				LARGE_INTEGER Frequency;

				/*QueryPerformanceFrequency(&Frequency);
				QueryPerformanceCounter(&StartingTime);*/

				D3DLOCKED_RECT* captureData = captureTask->getCapture();
				if (captureData == NULL)
				{
					logOutput << "Failed to getCapture" << std::endl;
					captureTask->unlock();
					SetEvent(eventFrameRead); //Make sure we get back here
					continue;
				}

				/*QueryPerformanceCounter(&EndingTime);
				ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;
				ElapsedMicroseconds.QuadPart *= 1000000;
				ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;
				logOutput << "Time used getCapture: " << ElapsedMicroseconds.QuadPart << std::endl;*/

				//Send back new size
				captureTask->newWidth = viewFrameData->newWidth;
				captureTask->newHeight = viewFrameData->newHeight;

				//Encode frame
				int width = captureTask->getWidth();
				int height = captureTask->getHeight();
				int pitch = captureData->Pitch;
				char* data = (char*)captureData->pBits;

				if (encoder == NULL || captureTaskVersion != captureTask->getVersion())
				{
					captureTaskVersion = captureTask->getVersion();
					freeEncoder();
					if (!initializeEncoder(width, height)) {
						LogCurrentTime();
						SetEvent(eventFrameRead);
						logOutput << "Failed to intiailize X264 encoder" << std::endl;
					}
				}

				viewFrameData->width = width;
				viewFrameData->height = height;
				D3DFORMAT format = captureTask->getFormat();
				if (format != D3DFMT_X8R8G8B8 && format != D3DFMT_A8R8G8B8) //TODO: Support more formats
					logOutput << "Unsuported format: " << format << std::endl;
				viewFrameData->pitch = pitch;

				if (encoder != NULL)
				{
					//Encode
					encodeFrame(data, pitch, width, height);

					//Send data to main app
					viewFrameData->dataSize = 0;
					for (int n = 0; n < nalCount; n++)
						viewFrameData->dataSize += nals[n].i_payload;

					//Wait for shared memory to be free(TODO: Handle event if main app crashes while using shared memory)
					result = WaitForSingleObject(eventFrameRead, 1000);
					if (result == WAIT_TIMEOUT)
					{
						LogCurrentTime(); logOutput << "Timeout while waiting for main app to read frame." << std::endl;
						continue;
					}
					else if (result != WAIT_OBJECT_0)
					{
						LogCurrentTime();
						logOutput << "Error while waiting for event FrameRead" << std::endl;
						break;
					}

					memcpy_s(viewFrameData->getData(), viewFrameData->maxDataSize, nals[0].p_payload, viewFrameData->dataSize);
				}
				else
					captureTask->unlock(); //Also unlocked inside encodeFrame

				//Notify main app that we have data ready
				SetEvent(eventFrameReady);
			}
		}
	}

	//Clean hooks before events, or the hooked app can crash as it tries to set an event in a hook.
	cleanHooks();

freeEventHandles:
	closeEvents();
exitAndDetach:
	cleanHooks();
	freeEncoder();

	if (dummyWindow != NULL)
	{
		CloseWindow(dummyWindow);
		UnregisterClass(classNameDummyWindow, NULL);
	}

	if (captureTask != NULL)
	{
		delete captureTask;
		captureTask = NULL;
	}

	closeSharedMemory();

	LogCurrentTime();
	logOutput << "End Capture Thread" << endl;
	logOutput.flush();
	FreeLibraryAndExitThread(dllModule, 0);
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	//We assume that at the point of this DLL being injected, all the other DLL's have already
	//been loaded, thus we can use non kernel32 functions.
	if (!logOutput.is_open()) //TODO: Move this out of DllMain, as we can cause DLL loading to freeze due to dependency.
		logOutput.open("hookLog.txt", ios_base::out | ios_base::trunc);

	wchar_t name[MAX_PATH];

	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		dllModule = hModule;

		//GetModuleFileNameW(hModule, name, MAX_PATH);
		//LoadLibrary(name);

		//Create the thread that will do the actual hooking and capturing
		HANDLE mainThreadHandle = OpenThread(THREAD_ALL_ACCESS, NULL, GetCurrentThreadId());
		captureThreadHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)CaptureThread, (LPVOID)mainThreadHandle, 0, 0);
		if (!captureThreadHandle)
		{
			LogCurrentTime();
			logOutput << "Failed to create Capture thread" << endl;
			CloseHandle(mainThreadHandle);
			return false;
		}
	} else if(ul_reason_for_call == DLL_PROCESS_DETACH) {
		LogCurrentTime(); logOutput << "Starting DLL detach..." << endl;
		SetEvent(eventEndCapture);

		LogCurrentTime(); logOutput << "DLL detached!" << endl;
		logOutput.flush();
	}

	return true;
}


bool initializeEvents() {
#define LOGANDFAIL(strstream) { logOutput << strstream << endl; return false; }

	eventEndCapture = OpenEvent(EVENT_ALL_ACCESS, false, GCOEVENT_END_CAPTURE);
	if (eventEndCapture == NULL)
		LOGANDFAIL("Failed to open EndCapture event. Error: " << GetLastError());

	eventFrameReady = OpenEvent(EVENT_ALL_ACCESS, false, GCOEVENT_FRAME_READY);
	if (eventFrameReady == NULL)
		LOGANDFAIL("Failed to open FrameReady event. Error: " << GetLastError());

	eventFrameRead = OpenEvent(EVENT_ALL_ACCESS, false, GCOEVENT_FRAME_READ);
	if (eventFrameRead == NULL)
		LOGANDFAIL("Failed to open FrameRead event. Error: " << GetLastError());

	eventGotFrame = CreateEvent(NULL, false, false, GCOEVENT_GOT_FRAME);
	if (eventGotFrame == NULL)
		LOGANDFAIL("Failed to open GotFrame event. Error: " << GetLastError());
	ResetEvent(eventGotFrame);

	return true;
}

void closeEvents() {
	CloseHandle(eventEndCapture);
	CloseHandle(eventGotFrame);
	CloseHandle(eventFrameReady);
	CloseHandle(eventFrameRead);
}

bool initializeSharedMemory() {
	closeSharedMemory();

	sharedMemoryFrameData = OpenFileMapping(FILE_MAP_ALL_ACCESS, false, GCOMEMORY_FRAMEDATA);
	if (sharedMemoryFrameData == NULL)
	{
		LogCurrentTime(); logOutput << "Failed to open shared file mapping, error: " << GetLastError() << std::endl;
		return false;
	}

	int frameDataSize = sizeof(FrameData) + FrameData::maxDataSize; //This will be a few bytes more than we need
	viewFrameData = (FrameData*)MapViewOfFile(sharedMemoryFrameData, FILE_MAP_ALL_ACCESS, 0, 0, frameDataSize);
	if (viewFrameData == NULL)
	{
		LogCurrentTime(); logOutput << "Failed to map view of FrameData, error: " << GetLastError() << std::endl;
		CloseHandle(sharedMemoryFrameData);
		return false;
	}

	return true;
}

void closeSharedMemory() {

	if (viewFrameData)
	{
		UnmapViewOfFile(viewFrameData);
		viewFrameData = NULL;
	}

	if (sharedMemoryFrameData)
	{
		CloseHandle(sharedMemoryFrameData);
		sharedMemoryFrameData = NULL;
	}
}

void cleanHooks() {
	if (!isHooked)
		return;
	logOutput << "Unhooking" << std::endl;

	if (captureTask)
		captureTask->lock(false); //Make sure render thread doesn't do anything while we unhook

	hookEndScene.unHook();
	hookPresent.unHook();
	hookPresentEx.unHook();
	hookReset.unHook();
	hookSwapPresent.unHook();

	logOutput << "Unhooked" << std::endl;
	
	isHooked = false;
}


bool initializeEncoder(int width, int height) {
	//Set-up from: http://stackoverflow.com/questions/2940671/how-does-one-encode-a-series-of-images-into-h264-using-the-x264-c-api
	if (x264_param_default_preset(&param, "ultrafast", "zerolatency") != 0)
	{
		logOutput << "Error during x264_param_default_preset" << std::endl;
		return false;
	}

	int fps = 60; //TODO: Change this dynamically?

	//Height must be divisible by 2, so scale down for that
	float div = height / 2.0f;
	int newHeight = 2*(int)floor(div);

	div = width / 2.0f;
	int newWidth = 2*(int)floor(div);

	logOutput << "New X264 w: " << newWidth << " h: " << newHeight << std::endl;

	param.i_threads = 1; //TODO: Allow to configure
	param.i_width = newWidth;
	param.i_height = newHeight;
	param.i_fps_num = fps;
	param.i_fps_den = 1;

	param.i_keyint_max = fps;
	param.b_intra_refresh = 1;

	param.rc.i_rc_method = X264_RC_CRF;
	param.rc.f_rf_constant = 25;
	param.rc.f_rf_constant_max = 35;

	param.b_repeat_headers = 1;
	param.b_annexb = 1;
	if (x264_param_apply_profile(&param, "baseline") != 0)
	{
		logOutput << "Error during x264_param_apply_profile" << std::endl;
		return false;
	}

	encoder = x264_encoder_open(&param);
	if (encoder == NULL)
	{
		logOutput << "Failed to open encoder" << std::endl;
		return false;
	}

	if (x264_picture_alloc(&pic_in, X264_CSP_I420, param.i_width, param.i_height) != 0)
	{
		x264_encoder_close(encoder);
		encoder = NULL;
		logOutput << "Failed to allocate picture" << std::endl;
		return false;
	}

	//TODO: Allow for other input formats
	convertCtx = sws_getContext(width, height, PIX_FMT_RGBA, newWidth, newHeight, PIX_FMT_YUV420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);
	if (convertCtx == NULL)
	{
		x264_encoder_close(encoder);
		encoder = NULL;
		x264_picture_clean(&pic_in);
		logOutput << "Failed to get libswscale context" << std::endl;
		return false;
	}

	LogCurrentTime();
	logOutput << "Initialized X264 encoder with width: " << newWidth << ", height: " << newHeight << std::endl;

	return true;
}

bool encodeFrame(char* data, int pitch, int width, int height) {
	if (encoder == NULL)
		return false;

	//single plane
	uint8_t* in_data[1] = { (uint8_t*)data };
	int stride[1] = { pitch };


	LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
	LARGE_INTEGER Frequency;

	/*QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&StartingTime);*/

	sws_scale(convertCtx, in_data, stride, 0, height, pic_in.img.plane, pic_in.img.i_stride);
	captureTask->unlock(); //We are now done with the raw data

	/*QueryPerformanceCounter(&EndingTime);
	ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;
	ElapsedMicroseconds.QuadPart *= 1000000;
	ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;
	logOutput << "Time used scale_convert: " << ElapsedMicroseconds.QuadPart << std::endl;*/


	/*QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&StartingTime);*/

	int frame_size = x264_encoder_encode(encoder, &nals, &nalCount, &pic_in, &pic_out);
	if (frame_size > 0)
	{
		//LogCurrentTime(); logOutput << "Encoded frame size: " << frame_size << std::endl;
	}
	else
	{
		LogCurrentTime(); logOutput << "Failed to encode frame, return value: " << frame_size << std::endl;
		return false;
	}

	/*QueryPerformanceCounter(&EndingTime);
	ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;
	ElapsedMicroseconds.QuadPart *= 1000000;
	ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;
	logOutput << "Time used encode: " << ElapsedMicroseconds.QuadPart << std::endl;*/

	return true;
}

void freeEncoder() {
	if (encoder == NULL)
		return;
	x264_encoder_close(encoder);
	encoder = NULL;
	x264_picture_clean(&pic_in);

	if (convertCtx != NULL)
		sws_freeContext(convertCtx);
	LogCurrentTime(); logOutput << "Freed encoder" << std::endl;
}
