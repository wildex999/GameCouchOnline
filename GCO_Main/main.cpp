

extern "C" {
#include <enet\enet.h> //Must be at top to avoid winsock redefinition errors
}

//Using SDL and standard IO
#include <SDL.h>
#include <SDL_syswm.h>
#include <Windows.h>
#include <Psapi.h> //For ModuleEnum
#include <time.h>
#include <vector>

#include "Output.h"
#include "Events.h"
#include "FrameData.h"
#include "AudioCapture.h"

#include "HostPlayer.h"
#include "HostController.h"
#include "HostFramePacket.h"
#include "InputData.h"

extern "C" {
#include <libavcodec\avcodec.h>
#include <libswscale\swscale.h>
#include <libswresample\swresample.h>
}

const int strBufferSize = 1024 * 1024;
char strBuffer[strBufferSize];

//Screen dimension constants
const int SCREEN_WIDTH = 640;
const int SCREEN_HEIGHT = 480;

char backend_d3d9[] = "d3d9.dll";
char backend_d3d10[] = "d3d10.dll";
char backend_d3d10_1[] = "d3d10_1.dll";
char backend_d3d11[] = "d3d11.dll";
char backend_dxgi[] = "dxgi.dll";
char backend_d3d8[] = "d3d8.dll;";
char backend_opengl32[] = "opengl32.dll";


AVCodec* decoderCodec = NULL;
AVCodecContext* decoderContext = NULL;
AVFrame* decoderFrame = NULL;
SwsContext* convertCtx = NULL;

AudioCapture audioCapture;

ENetHost* server = NULL;

ENetHost* client = NULL;
ENetPeer* clientPeer = NULL;
bool retryConnect = false;
char* setServerAddr;
short setServerPort;

bool clientAudioInitialized = false;
SDL_AudioDeviceID clientAudioDevice;
SDL_AudioSpec clientAudioSpec;

bool audioEncoderInitialized;
bool audioDecoderInitialized;
AVCodecContext* audioCodecContext = NULL;
AVCodec* audioCodecACC = NULL;
AVFrame* audioFrame = NULL;
SwrContext* audioResampleContext = NULL;

struct AudioPacket{
	char* data;
	int dataSize;
	int offset;
	bool reSampled;

	AudioPacket(char* iData, int iDataSize, int iOffset) { data = iData; dataSize = iDataSize; offset = iOffset; reSampled = false; }
};
std::vector<AudioPacket*> clientAudioBuffer;
std::vector<AudioPacket*> encoderAudioBuffer;


BOOL CALLBACK EnumWindowHandler(HWND hwnd, LPARAM lParam);
bool getValidBackend(HANDLE hProc, char** out_backend);
bool parseCommandLine(int argc, char* args[]);
bool injectIntoProcess(int pid, char* dllPath);

bool createSharedMemory();
void freeSharedMemory();
bool initializeEvents();
void closeEvents();

bool createRenderSurface(int width, int height);
void freeRenderSurface();

bool decodeFrame(char* data, int dataSize, int width, int height, int *gotFrame);
bool showFrame();

bool initializeDecoder();
bool decodeFrame(unsigned char* data, int dataSize);
void freeDecoder();

bool initServer(uint16_t port);
bool handleServer();
bool serverHandleInput(ENetEvent netEvent);
bool closeServer();

bool initClient(char* hostAddr, short port);
bool clientConnect();
bool handleClient();
bool closeClient();

bool initAudio(int samplesPerSecond, int channels, int bitsPerSample);
bool handleAudio(HostFramePacket* packet);
void freeAudio();

bool initAudioEncoder(int samplesPerSecond, int channels);
bool encodeAudio(char* data, int dataBytes, std::vector<AVPacket*>* packetsOut);
void freeAudioEncoder();

bool initAudioDecoder(int samplesPerSecond, int channels);
bool decodeAudio(char* data, int size);
void freeAudioDecoder();

//Events
HANDLE eventEndCapture;
HANDLE eventFrameReady;
HANDLE eventFrameRead;


HANDLE sharedMemoryFrameData = NULL;
FrameData* viewFrameData = NULL;

//The surface contained by the window
SDL_Surface* screenSurface = NULL;
SDL_Surface* renderSurface = NULL;
SDL_Renderer* renderer = NULL;
SDL_Texture* renderTexture = NULL;

//The window we'll be rendering to
SDL_Window* window = NULL;

//Command Line Input
bool gotHookProcessId = false;
int hookProcessId;
bool gotHookExeName = false;
char* hookExeName;
bool useAudio = true;

bool isServer = false;
bool isClient = false;
char* serverAddr = "127.0.0.1";

struct FindExe {
	char* in_exeName;
	int out_processId;
};

FILE* videoFile;
Uint32 frameTime;

int origWidth = 0;
int origHeight = 0;
int prevWidth = -1;
int prevHeight = -1;
int curWidth = 0;
int curHeight = 0;

bool windowResize = false;
bool resizeEvent = false;


void CurrentTimeString(char* out_string, int bufferSize) {
	time_t     now = time(0);
	struct tm  tstruct;
	localtime_s(&tstruct, &now);
	strftime(out_string, bufferSize, "%X: ", &tstruct);
}

void LogCurrentTime() {
	char buf[128];
	CurrentTimeString(buf, 128);
	Output("%s",buf);
}

int main(int argc, char* args[])
{
	FILE *out, *err;
	freopen_s(&out, "stdout.txt", "w", stdout);
	freopen_s(&err, "stderr.txt", "w", stderr);

	avcodec_register_all();

	//Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
	}
	else
	{
		//Create window
		window = SDL_CreateWindow("Game View", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
		if (window == NULL)
		{
			printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
		}
		else
		{
			//Get window surface
			screenSurface = SDL_GetWindowSurface(window);
			frameTime = SDL_GetTicks();

			if (enet_initialize() != 0)
			{
				Output("Failed to initialize enet\n");
				return 1;
			}

			parseCommandLine(argc, args);

			if (!isClient && !isServer)
			{
				isServer = true;
				gotHookExeName = true;

				//hookExeName = "GCO_Main.exe";
				//hookExeName = "Battle.net.exe";
				//hookExeName = "Rayman Origins.exe";
				hookExeName = "Rayman Legends.exe";
				//hookExeName = "ShovelKnight.exe";
				//hookExeName = "hl2.exe";
			}

			if (isServer)
			{
				if (!initServer(7777))
					return 1;
			}
			if (isClient)
			{
				if (!initClient(serverAddr, 7777))
					return 1;
			}

			bool quit = false;
			while (!quit) {
				if (isServer)
				{
					if (!handleServer())
						quit = true;
				}
				if (isClient)
				{
					if (!handleClient())
						quit = true;
				}

			}

			//Cleanup
			Debug("Cleanup\n");
			if (isServer)
				closeServer();
			if (isClient)
				closeClient();

			enet_deinitialize();

		}
	}

	//Destroy window
	SDL_DestroyWindow(window);

	//Quit SDL subsystems
	SDL_Quit();

	return 0;
}

bool injectIntoProcess(int pid, char* dllPath) {
	SetLastError(0);
	HANDLE hookProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, false, pid);
	if (hookProcessHandle == NULL)
	{
		Output("Failed to open process id: %d, error: %d\n", pid, GetLastError());
		return false;
	}

	DWORD exeStrLen = MAX_PATH;
	char exePath[MAX_PATH];
	if (QueryFullProcessImageName(hookProcessHandle, 0, exePath, &exeStrLen)) {
		Output("Process exe path: %s\n", exePath);
	}
	else
	{
		Output("Process exe path unknown!");
	}

	//Check if process has a valid backend we can hook into
	char* backend;
	if (!getValidBackend(hookProcessHandle, &backend))
	{
		Output("Process(%d) has no valid backend\n", pid);
		goto out_closeProcessHandle;
	}
	Debug("Got valid backend: %s\n", backend);

	//Inject the DLL
	SIZE_T pathLength = strlen(dllPath)+1; //Include zero

	//Allocate memory in the remote process to store the dll path
	LPVOID dllNameMemory = VirtualAllocEx(hookProcessHandle, NULL, pathLength, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (dllNameMemory == NULL)
	{
		Output("Failed to allocate memory in the remote process for DLL path. Error: %d\n", GetLastError());
		goto out_closeProcessHandle;
	}
	Debug("Created remote memory\n");

	if (!WriteProcessMemory(hookProcessHandle, dllNameMemory, dllPath, pathLength, NULL))
	{
		Output("Failed to write DLL path to remote process memory. Error: %d\n", GetLastError());
		goto out_freeVirtualMemory;
	}
	Debug("Wrote dll path to remote memory\n");

	DWORD threadId;
	HANDLE remoteThread = CreateRemoteThread(hookProcessHandle, NULL, 0, (LPTHREAD_START_ROUTINE)&LoadLibrary, dllNameMemory, 0, &threadId);
	if (remoteThread == NULL)
	{
		Output("Failed to create thread in remote process. Error: %d\n", GetLastError());
		goto out_freeVirtualMemory;
	}
	Debug("Created remote thread\n");


	//Wait for our DLL to be loaded
	if (WaitForSingleObject(remoteThread, 2000) != WAIT_OBJECT_0)
	{
		Output("Remote thread failed to run in given time.");
		goto out_closeThreadHandle;
	}

	DWORD exitCode;
	if (!GetExitCodeThread(remoteThread, &exitCode))
	{
		Output("Failed to get thread return value! Following data might be invalid!\n");
	}
	//Exit code is now the return value of LoadLibrary, which is the handle for the loaded DLL, or null for an error.
	//The handle is of no use outside the remote process space, so we only check if it succeeded in loading the DLL.
	if (exitCode == NULL)
	{
		Output("Failed to load DLL in remote thread. This could mean the DLL could not be found.\n");
		goto out_closeThreadHandle;
	}

	Debug("Injected DLL returned without error\n");

	return true;

out_closeThreadHandle:
	CloseHandle(remoteThread);
out_freeVirtualMemory:
	VirtualFreeEx(hookProcessHandle, dllNameMemory, 0, MEM_RELEASE);
out_closeProcessHandle:
	CloseHandle(hookProcessHandle);
	return false;
}

bool initializeEvents() {
	eventEndCapture = CreateEvent(NULL, false, false, GCOEVENT_END_CAPTURE);
	if (eventEndCapture == NULL)
	{
		Output("Failed to create EndCapture event! Error: %d\n", GetLastError());
		return false;
	}
	ResetEvent(eventEndCapture); //In case the event already existed due to lingering injected DLL(TODO: Fix)

	eventFrameReady = CreateEvent(NULL, false, false, GCOEVENT_FRAME_READY);
	if (eventFrameReady == NULL)
	{
		Output("Failed to create FrameReady event! Error: %d\n", GetLastError());
		return false;
	}
	ResetEvent(eventFrameReady);

	eventFrameRead = CreateEvent(NULL, false, true, GCOEVENT_FRAME_READ);
	if (eventFrameRead == NULL)
	{
		Output("Failed to create FrameRead event! Error: %d\n", GetLastError());
		return false;
	}
	SetEvent(eventFrameRead); //We want this one to be triggered from the start, so the first frame doesn't timeout
	
	return true;
}

void closeEvents() {
	CloseHandle(eventEndCapture);
}

//Parse the command line. Returns false if failed.
bool parseCommandLine(int argc, char* args[]) {
	if (argc < 2)
		return true;

	for (int i = 1; i < argc; i++) {
		char* command = args[i];
		if (strncmp(command, "-", 1) != 0) //All commands must start with '-'
		{
			Output("Invalid Command\n");
			return false;
		}

		if (strcmp(command, "-pid") == 0)
		{
			i++;
			if (i == argc)
			{
				Output("Missing value\n");
				return false;
			}
			hookProcessId = strtol(args[i], NULL, 0);
			gotHookProcessId = true;
		}
		else if (strcmp(command, "-exename") == 0)
		{
			i++;
			if (i == argc)
			{
				Output("Missing value\n");
				return false;
			}
			hookExeName = args[i];
			gotHookExeName = true;
		}
		else if (strcmp(command, "-server") == 0)
		{
			isServer = true;
		}
		else if (strcmp(command, "-client") == 0)
		{
			isClient = true;
		}
		else if (strcmp(command, "-addr") == 0)
		{
			i++;
			if (i == argc)
			{
				Output("Missing value\n");
				return false;
			}
			serverAddr = args[i];
		}
		else if (strcmp(command, "-noaudio") == 0)
		{
			useAudio = false;
		}
	}

	return true;
}

BOOL CALLBACK EnumWindowHandler(HWND hwnd, LPARAM lParam)
{
	const int windowClassStrLen = 256;
	const int windowExeStrLen = MAX_PATH;
	char windowClass[windowClassStrLen];
	char windowExe[windowExeStrLen];

	FindExe* findExe;
	if (lParam != NULL)
		findExe = (FindExe*)lParam;
	else
		findExe = NULL;

	//Secure against overflow
	windowClass[windowClassStrLen - 1] = 0;
	windowExe[windowExeStrLen - 1] = 0;

	if (!IsWindowVisible(hwnd))
		return true;

	GetClassName(hwnd, windowClass, windowClassStrLen);

	DWORD processId;
	GetWindowThreadProcessId(hwnd, &processId);
	char idStr[100];
	sprintf_s(idStr, 100, "ProcessId: %d\n", processId);
	Debug(idStr);

	HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, processId);
	if (!hProc) {
		Debug("Failed to OpenProcess: ");
		Debug(windowClass);
		Debug("\n");
		return true;
	}

	//Find valid Graphic backend
	char* backend;
	if (!getValidBackend(hProc, &backend))
		goto out_continue;


	//Find exe name
	DWORD exeStrLen = windowExeStrLen - 1;
	if (QueryFullProcessImageName(hProc, 0, windowExe, &exeStrLen)) {
		if (findExe)
		{
			if (exeStrLen > 0)
			{
				//Ignore path
				char* exeName = strrchr(windowExe, '\\');
				if (exeName == NULL)
					exeName = windowExe;
				else
					exeName++;

				if (strcmp(findExe->in_exeName, exeName) == 0)
				{
					findExe->out_processId = processId;
					goto out_end;
				}
			}
		}
		else
		{
			Output(idStr);
			Output("Name: ");
			Output(windowClass);
			Output(" | Exe: ");
			if (exeStrLen > 0)
			{
				Output(windowExe);
			}
			else
				Output("Unknown");
			Output(" | Backend: ");
			Output(backend);
			Output("\n");
		}
	}

out_continue:
	CloseHandle(hProc);
	return true;
out_end: //Stop iterating windows
	CloseHandle(hProc);
	return false;
}

bool getValidBackend(HANDLE hProc, char** out_backend) {
	HMODULE hMods[1024];
	DWORD size;
	char* backend = NULL;

	if (EnumProcessModulesEx(hProc, hMods, sizeof(hMods), &size, LIST_MODULES_ALL)) {
		int moduleCount = (size / sizeof(HMODULE));
		for (UINT m = 0; m < moduleCount; m++) {
			const int modulePathStrLen = MAX_PATH;
			char modulePath[modulePathStrLen];

			if (GetModuleFileNameEx(hProc, hMods[m], modulePath, modulePathStrLen - 1)) {
				char* moduleName = strrchr(modulePath, '\\');
				if (moduleName == NULL)
					moduleName = modulePath;
				else
					moduleName++;
				
				if (strcmp(moduleName, backend_d3d9) == 0)
					backend = backend_d3d9;
				/*else if (strcmp(moduleName, backend_d3d10) == 0)
					backend = backend_d3d10;
				else if (strcmp(moduleName, backend_d3d10_1) == 0)
					backend = backend_d3d10_1;
				else if (strcmp(moduleName, backend_d3d11) == 0)
					backend = backend_d3d11;
				else if (strcmp(moduleName, backend_dxgi) == 0)
					backend = backend_dxgi;
				else if (strcmp(moduleName, backend_d3d8) == 0)
					backend = backend_d3d8;
				else if (strcmp(moduleName, backend_opengl32) == 0)
					backend = backend_opengl32;*/

				if (backend != NULL)
					break;
			}
		}
	}

	if (backend == NULL)
		return false;
	*out_backend = backend;
	return true;
}

bool createSharedMemory() {
	freeSharedMemory();

	int frameDataSize = sizeof(FrameData) + FrameData::maxDataSize; //This will be a few bytes more than we need
	sharedMemoryFrameData = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, frameDataSize, GCOMEMORY_FRAMEDATA);
	if (sharedMemoryFrameData == NULL)
	{
		Output("Failed to create shared FrameData file mapping, error: %d\n", GetLastError());
		return false;
	}

	viewFrameData = (FrameData*)MapViewOfFile(sharedMemoryFrameData, FILE_MAP_ALL_ACCESS, 0, 0, frameDataSize);
	if (viewFrameData == NULL)
	{
		Output("Failed to map view of FrameData, error: %d\n", GetLastError());
		CloseHandle(sharedMemoryFrameData);
		sharedMemoryFrameData = NULL;
		return false;
	}
	viewFrameData->newWidth = viewFrameData->newHeight = 0;

	Debug("Created shared memory\n");

	return true;
}

void freeSharedMemory() {

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

	Debug("Freed shared memory\n");
}



bool handleFrame(char* data, int dataSize, int width, int height, int *gotFrame)
{

	LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
	LARGE_INTEGER Frequency;

	QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&StartingTime);


	if (origWidth == 0)
	{
		origWidth = width;
		origHeight = height;
		windowResize = true;
	}
	if (width != prevWidth || height != prevHeight)
	{
		prevWidth = width;
		prevHeight = height;
		windowResize = true;
	}

	if (windowResize)
	{
		windowResize = false;
		//TODO:Handle fullscreen
		//Maintain aspect ratio with original resolution
		float aspect = (float)origHeight/(float)origWidth;
		SDL_GetWindowSize(window, &curWidth, &curHeight);

		curHeight = curWidth*aspect;
		SDL_SetWindowSize(window, curWidth, curHeight);
		resizeEvent = false;

		screenSurface = SDL_GetWindowSurface(window);

		if (screenSurface->format->format != SDL_PIXELFORMAT_RGB888)
		{
			Output("Screen surface in non-suported format: %s\n", SDL_GetPixelFormatName(screenSurface->format->format));
			return false;
		}

		if (!createRenderSurface(width, height))
		{
			Output("Failed to create render surface\n");
			return false;
		}

		if (convertCtx != NULL)
		{
			sws_freeContext(convertCtx);
			convertCtx = NULL;
		}

		/*convertCtx = sws_getContext(width, height, PIX_FMT_YUV420P, convertWidth, convertHeight, PIX_FMT_RGBA, SWS_FAST_BILINEAR, NULL, NULL, NULL);
		if (convertCtx == NULL)
		{
			Output("Failed to create SwScale context\n");
			return false;
		}*/

		freeDecoder();
		if (!initializeDecoder())
		{
			Output("Failed to initialize decoder\n");
			return false;
		}

		Debug("Resize window to fit screen. Width: %d, height: %d\n", curWidth, curHeight);
	}

	//fwrite(viewFrameData->getData(), viewFrameData->dataSize, 1, videoFile);

	//Decode X264
	if (decodeFrame((unsigned char*)data, dataSize))
	{
		*gotFrame = true;
		/*

		memcpy_s(renderSurface->pixels,  decoderFrame->linesize[0] * decoderFrame->height, decoderFrame->data[0], renderSurface->pitch*renderSurface->h);
		SDL_UnlockSurface(renderSurface);

		SDL_BlitSurface(renderSurface, NULL, screenSurface, NULL);*/
	}

	QueryPerformanceCounter(&EndingTime);
	ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;
	ElapsedMicroseconds.QuadPart *= 1000000;
	ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;
	//Output("Time used Decode: %d\n", ElapsedMicroseconds.QuadPart);

	return true;
}

bool showFrame() {
	LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
	LARGE_INTEGER Frequency;

	QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&StartingTime);

	uint8_t* data;
	int pitch;
	//SDL_LockTexture(renderTexture, NULL, (void**)&data, &pitch);
	SDL_UpdateYUVTexture(renderTexture, NULL, decoderFrame->data[0], decoderFrame->linesize[0], decoderFrame->data[2], decoderFrame->linesize[2], decoderFrame->data[1], decoderFrame->linesize[1]);
	//memcpy_s(data, pitch*origHeight, decoderFrame->data[0], decoderFrame->linesize[0]);
	//sws_scale(convertCtx, decoderFrame->data, decoderFrame->linesize, 0, origHeight, (uint8_t**)&renderSurface->pixels, &renderSurface->pitch);
	//SDL_UnlockTexture(renderTexture);

	SDL_RenderCopy(renderer, renderTexture, NULL, NULL);

	//SDL_BlitScaled(renderSurface, NULL, screenSurface, NULL);

	QueryPerformanceCounter(&EndingTime);
	ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;
	ElapsedMicroseconds.QuadPart *= 1000000;
	ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;
	//Output("Time used showFrame: %d\n", ElapsedMicroseconds.QuadPart);

	return true;
}

bool createRenderSurface(int width, int height) {
	/*int xrgbFormat = SDL_DEFINE_PIXELFORMAT(SDL_PIXELTYPE_PACKED32, SDL_PACKEDORDER_XRGB, SDL_PACKEDLAYOUT_8888, 24, 4);
	int bpp;
	Uint32 Rmask, Gmask, Bmask, Amask;
	SDL_PixelFormatEnumToMasks(xrgbFormat, &bpp, &Rmask, &Gmask, &Bmask, &Amask);
	
	freeRenderSurface();
	renderSurface = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, bpp, Rmask, Gmask, Bmask, Amask);
	if (renderSurface == NULL)
	{
		Output("Failed to create render surface\n");
		return false;
	}
	Debug("Created renderSurface with width: %d, height: %d, bpp: %d\n", width, height, bpp);


	return true;*/

	renderTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STATIC, width, height);
	if (renderTexture == NULL)
	{
		Output("Failed to create Render Texture, error: %d\n", SDL_GetError());
		return false;
	}

}

void freeRenderSurface() {
	if (renderSurface == NULL)
		return;
	SDL_FreeSurface(renderSurface);
}

bool initializeDecoder() {
	decoderCodec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (decoderCodec == NULL)
	{
		Output("Failed to find decoder codec\n");
		return false;
	}

	decoderContext = avcodec_alloc_context3(decoderCodec);
	if (decoderContext == NULL)
	{
		Output("Failed to allocate decoder context\n");
		return false;
	}

	if (avcodec_open2(decoderContext, decoderCodec, 0) != 0)
	{
		Output("Failed to open decoder\n");
		avcodec_free_context(&decoderContext);
		return false;
	}

	decoderFrame = av_frame_alloc();
	if (decoderFrame == NULL)
	{
		Output("Failed to allocate frame for decoder\n");
		avcodec_close(decoderContext);
		avcodec_free_context(&decoderContext);
		return false;
	}

	Debug("Initialized decoder\n");

	return true;
}

bool decodeFrame(unsigned char* data, int dataSize) {
	if (decoderContext == NULL)
		return false;

	AVPacket packet;
	av_init_packet(&packet);

	packet.data = data;
	packet.size = dataSize;

	int frameFinished = 0;
	int res = avcodec_decode_video2(decoderContext, decoderFrame, &frameFinished, &packet);
	if (res < 0)
	{
		Output("Decode failed: %d\n", res);
		return false;
	}

	if (frameFinished)
	{
		if (decoderContext->pix_fmt != AV_PIX_FMT_YUV420P)
			Output("Unknown pixel format: %d\n", decoderContext->pix_fmt);
		return true;
	}

	return false;
}

void freeDecoder() {
	if (decoderContext == NULL)
		return;

	avcodec_close(decoderContext);
	avcodec_free_context(&decoderContext); //Will null out the pointer too
}


bool initServer(uint16_t port) {
	if (!initializeEvents())
		return 1;
	if (!createSharedMemory())
		return 1;

	int processId = 0;
	if (gotHookExeName)
	{
		FindExe findExe;
		findExe.in_exeName = hookExeName;
		findExe.out_processId = -1;
		EnumWindows(EnumWindowHandler, (LPARAM)&findExe);

		if (findExe.out_processId != -1)
			processId = findExe.out_processId;
	}
	//char dllPath[] = "C:\\Users\\Wildex999\\documents\\visual studio 2013\\Projects\\GCO_Main\\Debug\\GCO_HookDLL.dll";
	char dllPath[] = "C:\\Users\\Wildex999\\documents\\visual studio 2013\\Projects\\GCO_Main\\Release\\GCO_HookDLL.dll";
	injectIntoProcess(processId, dllPath);
	hookProcessId = processId;


	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = port;

	server = enet_host_create(&address, 32, 2, 0, 0);
	if (server == NULL)
	{
		Output("Failed to start host on port: %d\n", port);
		return false;
	}

	return true;
}

bool handleServer() {
	SDL_Event ev;
	while (SDL_PollEvent(&ev) != 0)
	{
		if (ev.type == SDL_QUIT)
			return false;
	}

	LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
	LARGE_INTEGER Frequency;

	QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&StartingTime);

	//Handle frame if ready
	DWORD result = WaitForSingleObject(eventFrameReady, 1);
	if (result == WAIT_OBJECT_0)
	{
		if (origWidth == 0)
		{
			origWidth = viewFrameData->width;
			origHeight = viewFrameData->height;
			viewFrameData->newWidth = origWidth / 2;
			viewFrameData->newHeight = origHeight / 2;
		}
		if (viewFrameData->width != prevWidth || viewFrameData->height != prevHeight)
		{
			prevWidth = viewFrameData->width;
			prevHeight = viewFrameData->height;
		}

		if (server->connectedPeers > 0)
		{
			//Start capturing audio on the first frame, or there will be a second of delay between image and audio
			char* audioData = NULL;
			int audioSize = 0;
			std::vector<AVPacket*> audioPackets;
			if (useAudio)
			{
				if (!audioCapture.isInitialized())
					audioCapture.startCapture();
				if (!audioEncoderInitialized)
					initAudioEncoder(audioCapture.getSamplesPerSecond(), audioCapture.getChannelCount());

				if (!audioCapture.getData(&audioData, &audioSize))
				{
					Output("Failed to get audio data\n");
					audioSize = 0;
				}

				encodeAudio(audioData, audioSize, &audioPackets);

				audioSize = 0;
				for (int f = 0; f < audioPackets.size(); f++)
					audioSize += audioPackets[f]->size;
			}

			//Construct video and audio packet
			HostFramePacket* frame = HostFramePacket::allocateFrame(viewFrameData->dataSize + FF_INPUT_BUFFER_PADDING_SIZE, audioSize + FF_INPUT_BUFFER_PADDING_SIZE);

			if (viewFrameData->dataSize > 0)
			{
				frame->width = viewFrameData->width;
				frame->height = viewFrameData->height;
				memcpy_s(frame->videoData, viewFrameData->dataSize, viewFrameData->getData(), viewFrameData->dataSize);
			}

			frame->bitsPerSample = audioCapture.getBitsPerSample();
			frame->channelCount = audioCapture.getChannelCount();
			frame->samplesPerSecond = audioCapture.getSamplesPerSecond();
			if (audioSize > 0)
			{
				int offset = 0;
				for (int f = 0; f < audioPackets.size(); f++)
				{
					AVPacket* curPacket = audioPackets[f];
					memcpy_s(&frame->audioData[offset], frame->audioByteCount, curPacket->data, curPacket->size);
					offset += curPacket->size;
					
					av_free_packet(curPacket);
				}
			}

			//Create and broadcats packet
			ENetPacket* packet = enet_packet_create(frame, frame->frameSize, ENET_PACKET_FLAG_RELIABLE | ENET_PACKET_FLAG_NO_ALLOCATE);
			packet->freeCallback = HostFramePacket::freeCallback; //Make sure the data is deleted properly
			enet_host_broadcast(server, 0, packet);
		}

		SetEvent(eventFrameRead);
	}
	else if (result != WAIT_TIMEOUT)
	{
		Output("Error while waiting for frame.\n");
		return false;
	}

	//Handle network packets
	ENetEvent netEvent;
	while (enet_host_service(server, &netEvent, 1) > 0)
	{
		switch (netEvent.type)
		{
		case ENET_EVENT_TYPE_CONNECT:
			Output("Got new peer\n");
			netEvent.peer->data = new HostPlayer(netEvent.peer);

			break;
		case ENET_EVENT_TYPE_RECEIVE:
			//Handle controller input

			serverHandleInput(netEvent);

			break;
		case ENET_EVENT_TYPE_DISCONNECT:
			Output("Lost peer\n");
			delete netEvent.peer->data;

			break;
		}
	}

	QueryPerformanceCounter(&EndingTime);
	ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;
	ElapsedMicroseconds.QuadPart *= 1000000;
	ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;
	Debug("Server loop time: %d\n", ElapsedMicroseconds.QuadPart);

	//SDL_UpdateWindowSurface(window);

	return true;
}

bool serverHandleInput(ENetEvent netEvent) {

	HWND inputWindow = GetForegroundWindow();
	DWORD id;
	GetWindowThreadProcessId(inputWindow, &id);
	if (id != hookProcessId)
	{
		Debug("Ignore client input, incorrect window in focus\n");
		return false; //We only handle input if our hooked application get's the input
	}

	//TODO: Check size and type etc. first
	InputKeyboard* inputKb = (InputKeyboard*)netEvent.packet->data;

	INPUT* input = new INPUT();
	input->type = INPUT_KEYBOARD;
	input->ki.time = 0;
	input->ki.dwExtraInfo = NULL;
	input->ki.dwFlags = 0;
	if (inputKb->scanCode)
	{
		input->ki.dwFlags |= KEYEVENTF_SCANCODE;
		input->ki.wScan = inputKb->key;
	}
	else
		input->ki.wVk = inputKb->key;

	if (inputKb->keyUp)
		input->ki.dwFlags |= KEYEVENTF_KEYUP;

	if (SendInput(1, input, sizeof(INPUT)) <= 0)
		Output("Failed to insert input from client, error: %d\n", GetLastError());

	delete input;

	return true;
}

bool closeServer() {
	SetEvent(eventEndCapture);
	freeSharedMemory();
	audioCapture.endCapture();

	if (server)
		enet_host_destroy(server);
	server = NULL;

	return true;
}



bool initClient(char* hostAddr, short port) {

	setServerAddr = hostAddr; //Set for connection retry
	setServerPort = port;
	if (!clientConnect())
		return false;

	//Create renderer for hardware accelerated pixel format change and scaling
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (renderer == NULL)
	{
		Output("Failed to create SDL Renderer, error: %d\n", SDL_GetError());
		return false;
	}

	//Listen to Windows events for input scancodes
	SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);

	return true;
}

bool clientConnect() {
	if (!client)
	{
		client = enet_host_create(NULL, 1, 2, 0, 0);
		if (client == NULL)
		{
			Output("Failed to create client\n");
			return false;
		}
	}

	ENetAddress address;
	enet_address_set_host(&address, setServerAddr);
	address.port = setServerPort;

	clientPeer = enet_host_connect(client, &address, 2, 0);
	if (clientPeer == NULL)
	{
		Output("Failed to initiate connection to host: %s:%d\n", setServerAddr, setServerPort);
		enet_host_destroy(client);
		return false;
	}

	//Wait for connection so complete
	ENetEvent netEvent;
	if (enet_host_service(client, &netEvent, 1000) > 0 && netEvent.type == ENET_EVENT_TYPE_CONNECT)
	{
		Output("Connection complete to host: %s:%d\n", setServerAddr, setServerPort);
		return true;
	}
	
	enet_peer_reset(clientPeer);
	Output("Failed to connect to host: %s:%d\n", setServerAddr, setServerPort);
	return false;
}

bool handleClient() {

	//Get input and send to host
	SDL_Event ev;
	while (SDL_PollEvent(&ev) != 0)
	{
		if (ev.type == SDL_QUIT)
			return false;
		else if (ev.type == SDL_WINDOWEVENT)
		{
			if (ev.window.event == SDL_WINDOWEVENT_RESIZED)
			{
				windowResize = true;
				resizeEvent = true;
			}

		}
		else if (ev.type == SDL_SYSWMEVENT)
		{
			HWND hwnd = ev.syswm.msg->msg.win.hwnd;
			UINT msg = ev.syswm.msg->msg.win.msg;
			WPARAM wParam = ev.syswm.msg->msg.win.wParam;
			LPARAM lParam = ev.syswm.msg->msg.win.lParam;
			InputKeyboard inputKb;

			unsigned short scanCode;
			bool isExtended;
			ENetPacket* packet;

			switch (msg)
			{
			case WM_KEYDOWN:
				scanCode = (lParam >> 16) & 0xFF;
				isExtended = (lParam >> 24) & 0x01;

				inputKb.scanCode = true;
				inputKb.keyUp = false;
				inputKb.key = scanCode;
				inputKb.extKey = isExtended;

				packet = enet_packet_create(&inputKb, sizeof(InputKeyboard), ENET_PACKET_FLAG_RELIABLE);
				enet_peer_send(clientPeer, 1, packet);

				break;
			case WM_KEYUP:
				scanCode = (lParam >> 16) & 0xFF;
				isExtended = (lParam >> 24) & 0x01;

				inputKb.scanCode = true;
				inputKb.keyUp = true;
				inputKb.key = scanCode;
				inputKb.extKey = isExtended;

				packet = enet_packet_create(&inputKb, sizeof(InputKeyboard), ENET_PACKET_FLAG_RELIABLE);
				enet_peer_send(clientPeer, 1, packet);

				break;
			}
		}
	}

	//Check for network data from host
	ENetEvent netEvent;
	int gotFrame = 0;
	int maxEvents = 100;
	int framesSkipped = 0;
	while (enet_host_service(client, &netEvent, 0) > 0 && maxEvents > 0)
	{
		maxEvents--;
		switch (netEvent.type)
		{
		case ENET_EVENT_TYPE_RECEIVE:
			//Handle Video and Audio
			if (netEvent.channelID == 0)
			{
				HostFramePacket* framePacket = HostFramePacket::initFrame((char*)netEvent.packet->data);
				//Output("Got frame packet. Video size: %d\n", framePacket->videoByteCount);
				handleFrame(framePacket->videoData, framePacket->videoByteCount - FF_INPUT_BUFFER_PADDING_SIZE, framePacket->width, framePacket->height, &gotFrame);
				if (gotFrame)
					framesSkipped++;
				//TODO: Render last frame instead of first

				//Handle audio
				if (framePacket->audioByteCount > 0)
				{
					if (useAudio)
					{
						if (!clientAudioInitialized)
							initAudio(framePacket->samplesPerSecond, framePacket->channelCount, framePacket->bitsPerSample);
						if (!audioDecoderInitialized)
							initAudioDecoder(framePacket->samplesPerSecond, framePacket->channelCount);
						handleAudio(framePacket);
					}
				}
			}

			enet_packet_destroy(netEvent.packet);

			break;
		case ENET_EVENT_TYPE_DISCONNECT:
			Output("Lost connection\n");
			retryConnect = true;
			break;
		}
	}

	if (retryConnect)
	{
		if (clientConnect())
			retryConnect = false;
	}

	if (gotFrame)
	{
		framesSkipped--;
		Debug("Frames skipped: %d\n", framesSkipped);
		showFrame();
	}

	//Update the surface
	SDL_RenderPresent(renderer);

	return true;
}

bool closeClient() {
	if (clientPeer)
	{
		enet_peer_disconnect(clientPeer, 0);
		enet_host_flush(client);
	}
	clientPeer = NULL;
	if (client)
		enet_host_destroy(client);
	client = NULL;
	if (renderer)
		SDL_DestroyRenderer(renderer);
	renderer = NULL;
	return true;
}

bool speedUp = false;
int delayCounter = 0;
void audioReadCallback(void *userdata, Uint8 * stream, int len) {
	int streamOffset = 0;

	int bytesWritten = 0;
	while (clientAudioBuffer.size() > 0)
	{
		AudioPacket* audioPacket = clientAudioBuffer.front();

		if (!audioPacket->reSampled && speedUp && audioPacket->offset == 0)
		{
			SDL_AudioCVT cvt;
			int convOk = SDL_BuildAudioCVT(&cvt, AUDIO_F32, clientAudioSpec.channels, clientAudioSpec.freq, AUDIO_F32, clientAudioSpec.channels, clientAudioSpec.freq * 0.9);
			if (convOk == 1)
			{
				cvt.len = audioPacket->dataSize - audioPacket->offset;
				int bufSize = cvt.len*cvt.len_mult;
				cvt.buf = new uint8_t[bufSize];

				memcpy_s(cvt.buf, bufSize, &audioPacket->data[audioPacket->offset], audioPacket->dataSize - audioPacket->offset);
				SDL_ConvertAudio(&cvt);
				delete audioPacket->data;
				audioPacket->data = (char*)cvt.buf;
				audioPacket->offset = 0;
				audioPacket->dataSize = cvt.len_cvt;
				audioPacket->reSampled = true;
			}
			else
			{
				Output("Invalid audio convert: %d\n", convOk);
			}
		}

		int availableInPacket = audioPacket->dataSize - audioPacket->offset;
		int remainingInStream = len - streamOffset;

		if (availableInPacket > remainingInStream)
		{
			//Copy part of packet
			memcpy_s(&stream[streamOffset], remainingInStream, &audioPacket->data[audioPacket->offset], remainingInStream);
			audioPacket->offset += remainingInStream;
			bytesWritten += remainingInStream;
			break;
		}
		else
		{
			//Copy remaining of packet
			memcpy_s(&stream[streamOffset], remainingInStream, &audioPacket->data[audioPacket->offset], availableInPacket);
			streamOffset += availableInPacket;
			bytesWritten += availableInPacket;

			delete audioPacket->data;
			delete audioPacket;
			clientAudioBuffer.erase(clientAudioBuffer.begin());

			if (streamOffset == len)
				break; //Filled stream
		}
	}

	//Debug("Audio Frames: %d\n", clientAudioBuffer.size());
	int bufferedSamples = 0;
	for (int i = 0; i < clientAudioBuffer.size(); i++)
	{
		AudioPacket* audioPacket = clientAudioBuffer[i];
		bufferedSamples += ((audioPacket->dataSize - audioPacket->offset) / (clientAudioSpec.channels * 4)); //Assumes 32bit samples
	}
	int msBuffered = (int)floor(((float)bufferedSamples / (float)clientAudioSpec.freq) * 1000.0);
	Debug("Audio buffer: %d samples, %d ms, delayCounter: %d\n", bufferedSamples, msBuffered, delayCounter);

	//Check if we are lagging behind, and correct next time
	if (msBuffered > 50)
		delayCounter++;
	else
		delayCounter = 0;

	if (msBuffered > 50 && delayCounter >= 60) //Average packet time 2*
	{
		speedUp = true;
	}
	else if (msBuffered <= 25) //Average packet time
	{
		speedUp = false;
	}


	if (bytesWritten < len)
	{
		//Fill remaining with silence
		memset(&stream[bytesWritten], clientAudioSpec.silence, len - bytesWritten);
	}

}

bool initAudio(int samplesPerSecond, int channels, int bitsPerSample) {
	if (clientAudioInitialized)
		return false;

	SDL_zero(clientAudioSpec);

	clientAudioSpec.freq = samplesPerSecond;
	clientAudioSpec.format = AUDIO_F32; //TODO send correct(8, 16 bit = int, 32bit = float)
	clientAudioSpec.channels = channels;
	clientAudioSpec.samples = 1024; //TODO: Calculate buffer according to samplerate etc.
	clientAudioSpec.callback = audioReadCallback;

	clientAudioDevice = SDL_OpenAudio(&clientAudioSpec, NULL);
	if (clientAudioDevice < 0)
	{
		Output("Failed to create SDL Audio Device\n");
		return false;
	}

	SDL_PauseAudio(0);

	clientAudioInitialized = true;

	return true;
}

bool handleAudio(HostFramePacket* packet) {
	if (!clientAudioInitialized)
		return false;

	decodeAudio(packet->audioData, packet->audioByteCount - FF_INPUT_BUFFER_PADDING_SIZE);

	return true;
}

void freeAudio() {
	clientAudioInitialized = false;

	if (clientAudioDevice)
		SDL_CloseAudioDevice(clientAudioDevice);
	clientAudioDevice = NULL;

	for (int i = 0; i < clientAudioBuffer.size(); i++)
	{
		delete clientAudioBuffer[i]->data;
		delete clientAudioBuffer[i];

	}
	clientAudioBuffer.clear();
}


bool initAudioEncoder(int samplesPerSecond, int channels) {
	if (audioEncoderInitialized)
		return false;

	audioCodecACC = avcodec_find_encoder(CODEC_ID_AAC);
	if (audioCodecACC == NULL)
	{
		Output("Failed to find AAC encoder\n");
		return false;
	}

	audioCodecContext = avcodec_alloc_context3(audioCodecACC);
	if (audioCodecContext == NULL)
	{
		Output("Failed to allocate audio encoder context\n");
		return false;
	}
	audioCodecContext->bit_rate = 128 * 1024; //128kb/s
	audioCodecContext->sample_fmt = AV_SAMPLE_FMT_S16;
	audioCodecContext->sample_rate = samplesPerSecond;
	audioCodecContext->channels = channels;
	audioCodecContext->profile = FF_PROFILE_AAC_MAIN;

	audioCodecContext->time_base.num = 1;
	audioCodecContext->time_base.den = samplesPerSecond;

	audioCodecContext->codec_type = AVMEDIA_TYPE_AUDIO;

	int result = avcodec_open2(audioCodecContext, audioCodecACC, NULL);
	if (result != 0)
	{
		Output("Failed to open audio encoder, return: %d\n", result);
		freeAudioEncoder();
		return false;
	}

	audioFrame = av_frame_alloc();

	Output("Framesize: %d\n", audioCodecContext->frame_size);

	audioEncoderInitialized = true;
}

//framesOut should be a cleared vector
bool encodeAudio(char* data, int dataBytes, std::vector<AVPacket*>* packetsOut) {
	if (!audioEncoderInitialized)
		return false;

	//Add audio data to encode buffer
	int dataOffset = 0;
	int aacBatchSize = audioCodecContext->frame_size; //AAC require batches of 1024 frames to encode
	int frameSize = audioCodecContext->channels * 4; //Assumes 32 bit float for now(TODO: Fix)
	bool newPacket = false;
	while (dataBytes - dataOffset > 0)
	{
		AudioPacket* audioPacket;
		if (encoderAudioBuffer.size() == 0 || newPacket)
		{
			audioPacket = new AudioPacket(new char[frameSize*aacBatchSize], frameSize*aacBatchSize, 0);
			encoderAudioBuffer.push_back(audioPacket);
			newPacket = false;
		}
		else
			audioPacket = encoderAudioBuffer.back();
		if (audioPacket->dataSize - audioPacket->offset == 0)
		{
			newPacket = true;
			continue;
		}

		int leftInPacket = audioPacket->dataSize - audioPacket->offset;
		int remainingData = dataBytes - dataOffset;

		if (leftInPacket > remainingData)
		{
			//Copy remaining data to packet
			memcpy_s(&audioPacket->data[audioPacket->offset], leftInPacket, &data[dataOffset], remainingData);
			audioPacket->offset += remainingData;
			dataOffset += remainingData;
		}
		else
		{
			//Fill packet
			memcpy_s(&audioPacket->data[audioPacket->offset], leftInPacket, &data[dataOffset], leftInPacket);
			dataOffset += leftInPacket;
			audioPacket->offset += leftInPacket;
			newPacket = true;
		}
	}

	//Debug("Audio encode buffer size: %d\n", encoderAudioBuffer.size());

	//Convert and encode batches
	while (encoderAudioBuffer.size() > 0)
	{
		AudioPacket* audioPacket = encoderAudioBuffer.front();
		if (audioPacket->dataSize - audioPacket->offset != 0)
			break; //No more filled batches

		SDL_AudioCVT cvt;
		int convOk = SDL_BuildAudioCVT(&cvt, AUDIO_F32, audioCodecContext->channels, audioCodecContext->sample_rate, AUDIO_S16, audioCodecContext->channels, audioCodecContext->sample_rate);
		if (convOk == 1)
		{
			cvt.len = audioPacket->dataSize;
			int bufSize = cvt.len*cvt.len_mult;
			cvt.buf = new uint8_t[bufSize];

			memcpy_s(cvt.buf, bufSize, &audioPacket->data[0], audioPacket->dataSize);
			SDL_ConvertAudio(&cvt);
			delete audioPacket->data;
			audioPacket->data = (char*)cvt.buf;
			audioPacket->offset = 0;
			audioPacket->dataSize = cvt.len_cvt;
			audioPacket->reSampled = true;
		}
		else
		{
			Output("Invalid audio convert: %d\n", convOk);
			return false;
		}

		audioFrame->data[0] = (uint8_t*)audioPacket->data;
		audioFrame->linesize[0] = audioPacket->dataSize;
		audioFrame->extended_data = audioFrame->data;
		audioFrame->nb_samples = aacBatchSize;
		audioFrame->format = audioCodecContext->sample_fmt;
		audioFrame->sample_rate = audioCodecContext->sample_rate;

		AVPacket* packetOut = new AVPacket();
		av_init_packet(packetOut);
		packetOut->data = NULL;
		int gotPacket = 0;

		if (avcodec_encode_audio2(audioCodecContext, packetOut, audioFrame, &gotPacket) != 0)
			Output("Failed to encode audio\n");

		if (gotPacket != 0)
		{
			packetsOut->push_back(packetOut);
			//Debug("Encoded audio size: %d\n", packetOut->size);
		}
		else
			Debug("Got empty packet from audio encoder\n");

		delete audioPacket->data;
		delete audioPacket;
		encoderAudioBuffer.erase(encoderAudioBuffer.begin());
	}

	return true;
}

void freeAudioEncoder() {

	audioEncoderInitialized = false;

	if (audioCodecContext)
		avcodec_free_context(&audioCodecContext);
	if (audioFrame)
		av_frame_free(&audioFrame);

	for (int i = 0; i < encoderAudioBuffer.size(); i++)
	{
		AudioPacket* packet = encoderAudioBuffer[i];
		delete packet->data;
		delete packet;
	}
	encoderAudioBuffer.clear();
}


bool initAudioDecoder(int samplesPerSecond, int channels) {
	if (audioDecoderInitialized)
		return false;

	audioCodecACC = avcodec_find_decoder(CODEC_ID_AAC);
	if (audioCodecACC == NULL)
	{
		Output("Failed to find AAC decoder\n");
		return false;
	}

	audioCodecContext = avcodec_alloc_context3(audioCodecACC);
	if (audioCodecContext == NULL)
	{
		Output("Failed to allocate audio decoder context\n");
		return false;
	}

	audioCodecContext->channels = channels;
	audioCodecContext->sample_rate = samplesPerSecond;
	audioCodecContext->sample_fmt = AV_SAMPLE_FMT_S16;

	int result = avcodec_open2(audioCodecContext, audioCodecACC, NULL);
	if (result != 0)
	{
		Output("Failed to open audio decoder, return: %d\n", result);
		freeAudioEncoder();
		return false;
	}

	audioFrame = av_frame_alloc();

	Output("Framesize: %d\n", audioCodecContext->frame_size);


	audioDecoderInitialized = true;
}

bool decodeAudio(char* data, int size) {
	if (!audioDecoderInitialized)
		return false;

	AVPacket inPacket;
	av_init_packet(&inPacket);


	int len = 0;
	int offset = 0;
	while (true)
	{
		inPacket.size = size-offset;
		inPacket.data = (uint8_t*)&data[offset];

		int gotFrame = 0;
		len = avcodec_decode_audio4(audioCodecContext, audioFrame, &gotFrame, &inPacket);
		if (len < 0)
		{
			Output("Error while decoding audio: %d\n", len);
			return false;
		}

		offset += len;

		//Write decoded data to client buffer
		if (gotFrame)
		{
			//Change the format from Float planar(FFMPEG AAC out) to float non-planar(SDL Audio)
			if (audioFrame->format != AV_SAMPLE_FMT_FLTP)
				Output("ERROR: Expected Planar Float from audio decoder, got: %d. 'There be noise ahead!'\n", audioFrame->format);
			if (audioResampleContext == NULL)
			{
				audioResampleContext = swr_alloc_set_opts(NULL, audioFrame->channel_layout, AV_SAMPLE_FMT_FLT, audioFrame->sample_rate, audioFrame->channel_layout, (AVSampleFormat)audioFrame->format, audioFrame->sample_rate, 0, NULL);
				swr_init(audioResampleContext);
			}

			int bufferSize = audioFrame->channels*audioFrame->nb_samples * 4; //Allocate space for 4 byte float
			char* audioBuffer = new char[bufferSize]; 
			if (swr_convert(audioResampleContext, (uint8_t**)&audioBuffer, audioFrame->nb_samples, (const uint8_t**)audioFrame->extended_data, audioFrame->nb_samples) < 0)
				Output("Failed to convert decoded audio from floating planar to floating packed\n");

			/*int outSize = audioFrame->linesize[0];
			Debug("Out size: %d, format: %d\n", outSize, audioFrame->format);
			char* audioBuffer = new char[outSize];
			memcpy_s(audioBuffer, outSize, (char*)audioFrame->data[0], outSize);*/
			clientAudioBuffer.push_back(new AudioPacket(audioBuffer, bufferSize, 0));
		}

		if (size - offset == 0 || len == 0)
			break;
	}

	av_free_packet(&inPacket);

	return true;
}

void freeAudioDecoder() {
	
	audioDecoderInitialized = false;

	if (audioCodecContext)
		avcodec_free_context(&audioCodecContext);
	if (audioFrame)
		av_frame_free(&audioFrame);
	if (audioResampleContext)
		swr_free(&audioResampleContext);
}
