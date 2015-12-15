#ifndef CAPTURETASK_H_
#define CAPTURETASK_H_

#include <d3d9.h>

class D3D9CaptureTask {
	static short globalVersion;

	CRITICAL_SECTION mutex;
	bool readInProgress;
	bool initialized;
	bool useStretchSurface;
	short version; //Updated every time the task is reInitialized

	IDirect3DDevice9* device;
	IDirect3DSurface9* renderSurface;
	IDirect3DSurface9* stretchSurface;
	IDirect3DSurface9* cpuSurface;

	int currentWidth, currentHeight; //Current width and height

	D3DFORMAT format;
	D3DLOCKED_RECT lockRect;

	bool initializeSurfaces(int width, int height);
	void cleanupSurfaces();

public:
	int targetWidth, targetHeight; //The targeted width and height(Compared with new)
	int newWidth, newHeight; //Used by DLL to ask for resize of output surface

	D3D9CaptureTask(IDirect3DDevice9* device);
	~D3D9CaptureTask();

	bool reInitialize(IDirect3DDevice9* device); //Called whenever the d3d9 device is reset
	void deInitialize();
	bool isInitialized();

	//Lock the CaptureTask. Must be called before changing or using the CaptureTask.
	//If tryLock is true it will immediately return false if it failed to get a lock.
	bool lock(bool tryLock);
	void unlock();

	//Capture backbuffer and write it to a surface on system memory.
	//Called on D3D Thread
	bool captureToSystemMemory();

	//Return the pitch and byte data of the captured surface
	//Called on any thread
	D3DLOCKED_RECT* getCapture();

	//Called before captureToSystemMemory when getCapture is done.
	//Called on D3D Thread
	void freeCapture();

	int getWidth();
	int getHeight();
	D3DFORMAT getFormat();
	short getVersion() { return version; }

	IDirect3DSurface9* getRenderSurface() { return renderSurface; }

};

#endif
