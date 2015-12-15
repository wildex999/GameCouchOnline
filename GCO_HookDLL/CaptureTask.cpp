#include "stdafx.h"
#include "CaptureTask.h"
#include "common.h"

short D3D9CaptureTask::globalVersion = 0;

D3D9CaptureTask::D3D9CaptureTask(IDirect3DDevice9* device) {
	stretchSurface = NULL;
	cpuSurface = NULL;
	renderSurface = NULL;

	currentWidth = currentHeight = 0;
	targetWidth = currentHeight = 0;
	newWidth = newHeight = 0;

	initialized = false;
	InitializeCriticalSection(&mutex);

	reInitialize(device);
}

D3D9CaptureTask::~D3D9CaptureTask() {
	deInitialize();
	DeleteCriticalSection(&mutex);
}

void D3D9CaptureTask::deInitialize() {
	//Still a race condition here, but it's so small that we ignore it
	if (!initialized)
		return;
	lock(false);

	cleanupSurfaces();

	this->device = NULL;
	unlock();
}

bool D3D9CaptureTask::reInitialize(IDirect3DDevice9* device) {
	logOutput << "reInitialize CaptureTask" << std::endl;
	bool didLock = false;
	if (initialized)
	{
		lock(false);
		didLock = true;
		freeCapture();
	}
	version = ++globalVersion;

	readInProgress = false;
	initialized = false;
	this->device = device;

	if (!initializeSurfaces(newWidth, newHeight))
		return false;

	initialized = true;

	if (didLock)
		unlock();

	logOutput << "reInitialize done" << std::endl;

	return true;
}

bool D3D9CaptureTask::isInitialized() {
	return initialized;
}

//Set size to 0 to use the same as the render surface
bool D3D9CaptureTask::initializeSurfaces(int width, int height) {
	cleanupSurfaces();
	useStretchSurface = false;

	HRESULT result = device->GetRenderTarget(0, &renderSurface);
	//HRESULT result = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &renderSurface);
	if (renderSurface == NULL || FAILED(result))
	{
		LogCurrentTime(); logOutput << "Failed to get render target." << std::endl;
		return false;
	}

	D3DSURFACE_DESC surfaceInfo;
	result = renderSurface->GetDesc(&surfaceInfo);
	if (FAILED(result))
	{
		cleanupSurfaces();
		LogCurrentTime(); logOutput << "Failed to get render target surface description." << std::endl;
		return false;
	}

	if (width == 0)
		width = surfaceInfo.Width-4;
	if (height == 0)
		height = surfaceInfo.Height-4;
	targetWidth = newWidth;
	targetHeight = newHeight;
	currentWidth = width;
	currentHeight = height;

	bool forceStretched = true;
	if (forceStretched || surfaceInfo.Width != width || surfaceInfo.Height != height || surfaceInfo.MultiSampleType != D3DMULTISAMPLE_NONE)
	{
		result = device->CreateRenderTarget(width, height, surfaceInfo.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &stretchSurface, NULL);
		if (FAILED(result))
		{
			cleanupSurfaces();
			LogCurrentTime(); logOutput << "Failed to create Stretch surface with width: " << width << ", height: " << height << std::endl;
			return false;
		}

		LogCurrentTime(); logOutput << "Created Stretch surface" << std::endl;
		useStretchSurface = true;
	}

	result = device->CreateOffscreenPlainSurface(width, height, surfaceInfo.Format, D3DPOOL_SYSTEMMEM, &cpuSurface, NULL);
	if (FAILED(result))
	{
		cleanupSurfaces();
		LogCurrentTime(); logOutput << "Failed to create CPU surface with width: " << width << ", height: " << height << std::endl;
		return false;
	}

	this->format = surfaceInfo.Format;
	LogCurrentTime(); logOutput << "Created CPU surface with width: " << width << ", height: " << height << " format: " << surfaceInfo.Format << std::endl;

	initialized = true;
	return true;
}

void D3D9CaptureTask::cleanupSurfaces() {
	initialized = false;

	if (stretchSurface)
		stretchSurface->Release();
	stretchSurface = NULL;
	if (cpuSurface)
		cpuSurface->Release();
	cpuSurface = NULL;
	if (renderSurface)
		renderSurface->Release();
	renderSurface = NULL;

	LogCurrentTime(); logOutput << "Surfaces cleaned up" << std::endl;
}

bool D3D9CaptureTask::lock(bool tryLock) {
	if (tryLock)
		return TryEnterCriticalSection(&mutex);
	else
		EnterCriticalSection(&mutex);
	return true;
}

void D3D9CaptureTask::unlock() {
	LeaveCriticalSection(&mutex);
}

bool D3D9CaptureTask::captureToSystemMemory() {
	if (!initialized)
		return false;

	HRESULT result;

	IDirect3DSurface9* sourceSurface = renderSurface;

	if (useStretchSurface)
	{
		result = device->StretchRect(sourceSurface, NULL, stretchSurface, NULL, D3DTEXF_NONE);
		if (FAILED(result))
		{
			LogCurrentTime(); logOutput << "Failed to Stretch surface" << std::endl;
			return false;
		}
		sourceSurface = stretchSurface;
	}

	freeCapture();
	result = device->GetRenderTargetData(sourceSurface, cpuSurface);
	if (FAILED(result))
	{
		LogCurrentTime(); logOutput << "Failed to Get Render Target Data" << std::endl;
		return false;
	}

	RECT rect;
	rect.left = 0;
	rect.right = currentWidth;
	rect.top = 0;
	rect.bottom = currentHeight;

	result = cpuSurface->LockRect(&lockRect, &rect, D3DLOCK_READONLY);
	if (FAILED(result))
	{
		LogCurrentTime(); logOutput << "Failed to lock CPU surface" << std::endl;
		return false;
	}


	return true;
}

D3DLOCKED_RECT* D3D9CaptureTask::getCapture() {
	if (!initialized)
		return NULL;

	return &lockRect;
}

void D3D9CaptureTask::freeCapture() {
	cpuSurface->UnlockRect();
}


int D3D9CaptureTask::getWidth() {
	return currentWidth;
}

int D3D9CaptureTask::getHeight() {
	return currentHeight;
}

D3DFORMAT D3D9CaptureTask::getFormat() {
	return format;
}