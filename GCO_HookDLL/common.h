#ifndef COMMON_H_
#define COMMON_H_

#include <fstream>
#include <Windows.h>

#include "CaptureTask.h"
#include "../GCO_Main/Events.h"

extern std::wfstream logOutput;

extern const wchar_t* classNameDummyWindow;

extern D3D9CaptureTask* captureTask;

//Events
extern HANDLE eventEndCapture;
extern HANDLE eventFrameReady;
extern HANDLE eventFrameRead;

extern HANDLE eventGotFrame;


void CurrentTimeString(char* out_string, int bufferSize);
void LogCurrentTime();

#endif